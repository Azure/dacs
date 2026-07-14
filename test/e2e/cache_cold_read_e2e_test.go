// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

// Package e2e contains end-to-end tests for the DACS cache system.
//
// This file tests cache cold read coordination: when multiple clients request
// the same uncached file concurrently, only one client should download from
// blob storage while the others wait for the cache to be populated.
//
// # Running Tests
//
//	make deploy CONTROLLER=new HELM_ARGS="--set cache.enabled=true"
//	make test-e2e TEST_FILTER=cold-read
//
// # Prerequisites
//
//   - Cache server deployed and ready (StatefulSet with cache CRD)
//   - Storage account configured (--storage-account flag)
//   - Workload identity configured (--workload-identity-client-id, --workload-identity-sa)
package e2e

import (
	"context"
	"encoding/json"
	"fmt"
	"os/exec"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/kube/job"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/scenario"
)

const (
	coldReadTestBlobName = "cold-read-test/testfile.bin"
	// 50 MiB test file — large enough for multiple cache chunks, small enough
	// to keep the test fast.
	coldReadTestBlobSize  = 50 * 1024 * 1024
	coldReadConcurrency   = 3
	coldReadJobTimeout    = 10 * time.Minute
	coldReadCleanupLabel  = "dacs.azure.com/cold-read-test"
	coldReadCacheChunkMiB = 16
)

// coldReadJobResult is the JSON output from the read workload pods.
type coldReadJobResult struct {
	Pod          string  `json:"pod"`
	File         string  `json:"file"`
	Size         int     `json:"size"`
	ElapsedSec   float64 `json:"elapsed_sec"`
	ThroughputMB float64 `json:"throughput_mbps"`
}

var _ = Describe("Cache Cold Read Coordination", Label("e2e", "aks", "cold-read"), Ordered, func() {
	var (
		storageAccount   string
		storageContainer string
		testImage        string
		discoveryURL     string
		wiClientID       string
		wiSA             string
	)

	BeforeAll(func(ctx SpecContext) {
		// Cache readiness is verified by common.Setup() in BeforeSuite
		// (checks cacheserver StatefulSet exists and all replicas are ready).

		// Skip if storage account is not configured
		storageAccount = fluxOpts.StorageAccount
		if storageAccount == "" {
			Skip("Skipping cold read coordination tests — --storage-account not set")
		}
		storageContainer = fluxOpts.StorageContainer
		if storageContainer == "" {
			storageContainer = "test-data"
		}
		testImage = fluxOpts.ClientImage
		if testImage == "" {
			testImage = common.GetDefaultImage()
		}
		discoveryURL = common.GetDiscoveryURL(namespace)
		wiClientID = fluxOpts.WorkloadIdentityClientID
		wiSA = fluxOpts.WorkloadIdentitySA

		By("Provisioning test blob for cold read test")
		provisionColdReadTestBlob(ctx, k8sClient, storageAccount, storageContainer, testImage, wiClientID, wiSA)

		By("Flushing cache to ensure cold start")
		if err := scenario.FlushCache(ctx, discoveryURL); err != nil {
			log.Info("FlushCache warning (non-fatal)", "error", err)
		}
		time.Sleep(3 * time.Second)
	})

	AfterAll(func(ctx SpecContext) {
		By("Cleaning up cold read test jobs")
		cmd := exec.CommandContext(ctx, "kubectl", "-n", namespace,
			"delete", "jobs", "-l", coldReadCleanupLabel, "--ignore-not-found=true")
		if output, err := cmd.CombinedOutput(); err != nil {
			log.Info("Warning: cold read job cleanup had issues", "error", err, "output", string(output))
		}
	})

	It("should download from blob only once when multiple clients read concurrently", func(ctx SpecContext) {
		By("Scraping cache server metrics before test")
		metricsBefore := scenario.ScrapeCacheServerMetrics(ctx, namespace)

		By(fmt.Sprintf("Launching %d parallel read jobs", coldReadConcurrency))
		jobNames := make([]string, coldReadConcurrency)
		for i := 0; i < coldReadConcurrency; i++ {
			jobName := fmt.Sprintf("cold-read-%d", i)
			jobNames[i] = jobName
			j := buildColdReadJob(ctx, jobName, storageAccount, storageContainer,
				testImage, discoveryURL, wiClientID, wiSA)
			// Clean up any previous run (Delete returns an error if the job
			// doesn't exist, which is fine — we only care about timeout).
			_ = job.Delete(ctx, jobName, namespace)
			Expect(job.WaitForDeletion(ctx, jobName, namespace, 15*time.Second)).
				To(Succeed(), "Timed out waiting for previous job %s to be deleted", jobName)
			Expect(k8sClient.Create(ctx, j)).To(Succeed(), "Failed to create job %s", jobName)
		}

		By("Waiting for all read jobs to complete")
		for _, jn := range jobNames {
			job.WaitForCompletion(ctx, jn, namespace, coldReadJobTimeout)
		}

		By("Verifying all jobs succeeded and read the correct data")
		results := make([]*coldReadJobResult, 0, coldReadConcurrency)
		for _, jn := range jobNames {
			result := extractColdReadResult(ctx, jn)
			Expect(result.Size).To(Equal(coldReadTestBlobSize),
				"Job %s read wrong number of bytes", jn)
			results = append(results, result)
		}

		By("Scraping cache server metrics after test")
		metricsAfter := scenario.ScrapeCacheServerMetrics(ctx, namespace)
		delta := scenario.ComputeCacheMetricsDelta(metricsBefore, metricsAfter)

		log.Info("Cache cold read metrics delta",
			"downloadHits", delta.DownloadSuccess,
			"downloadMisses", delta.DownloadInvalidTransition,
			"uploadSuccess", delta.UploadSuccess,
			"uploadDuplicates", delta.UploadInvalidTransition)

		By("Asserting cache cold read behavior")

		expectedChunks := (coldReadTestBlobSize + coldReadCacheChunkMiB*1024*1024 - 1) / (coldReadCacheChunkMiB * 1024 * 1024)

		// Every client should route every chunk request through the cache server
		// (getting either a cache hit or a lock/miss response). With N clients
		// and C chunks, total cache-server download requests ≥ N × C.
		// If clients bypass the cache server and fall through directly to blob,
		// this count drops to near-zero — which is exactly the broken behavior
		// described in the cold-start spec.
		totalDownloads := delta.DownloadSuccess + delta.DownloadInvalidTransition
		minExpectedDownloads := expectedChunks * coldReadConcurrency / 2 // allow 50% margin
		log.Info("Cache server download request check",
			"totalDownloads", totalDownloads,
			"downloadHits", delta.DownloadSuccess,
			"downloadMisses", delta.DownloadInvalidTransition,
			"minExpected", minExpectedDownloads,
			"expectedIdeal", expectedChunks*coldReadConcurrency)
		Expect(totalDownloads).To(BeNumerically(">=", minExpectedDownloads),
			"Only %d cache-server download requests seen (expected ≥%d for %d clients × %d chunks). "+
				"Clients may be bypassing the cache server and downloading directly from blob",
			totalDownloads, minExpectedDownloads, coldReadConcurrency, expectedChunks)

		// With cold read, at least some clients must have received data from cache
		// (Download+Success > 0). Without cold read, all clients would download
		// from blob directly and there would be zero cache hits.
		Expect(delta.DownloadSuccess).To(BeNumerically(">", 0),
			"Expected cache hits > 0 — cold read should let waiting clients read from cache")

		// With cold read, only the lock-holding client uploads each chunk.
		// Upload+InvalidTransition (FILE_EXISTS) should be 0 because no other
		// client attempts to upload the same chunk.
		Expect(delta.UploadInvalidTransition).To(BeNumerically("==", 0),
			"Expected zero duplicate uploads — only one client should upload each chunk")

		// Sanity: at least some uploads happened (the winning client uploaded).
		Expect(delta.UploadSuccess).To(BeNumerically(">", 0),
			"Expected at least one successful upload to cache")

		// Cache misses (InvalidTransition) should be roughly one file's worth
		// of chunks — only the lock-holding client gets a miss per chunk.
		// With N clients, N-1 of them wait and eventually get cache hits.
		maxAcceptableMisses := int(float64(expectedChunks) * 1.5)
		log.Info("Blob download count check",
			"downloadMisses", delta.DownloadInvalidTransition,
			"expectedChunks", expectedChunks,
			"maxAcceptable", maxAcceptableMisses)
		Expect(delta.DownloadInvalidTransition).To(BeNumerically("<=", maxAcceptableMisses),
			"Too many cache misses (%d vs expected ~%d). Multiple clients may be "+
				"independently downloading the same chunks from blob instead of waiting",
			delta.DownloadInvalidTransition, expectedChunks)

		// Hit rate should be meaningful — with N=3 and cold read working, roughly
		// (N-1)/N of all download requests should be cache hits.
		hitRate := float64(delta.DownloadSuccess) / float64(totalDownloads)
		log.Info("Cache hit rate", "rate", fmt.Sprintf("%.1f%%", hitRate*100))
		Expect(hitRate).To(BeNumerically(">", 0.3),
			"Expected meaningful cache hit rate with %d concurrent readers", coldReadConcurrency)

		// Timing spread: with cold read coordination the lock-holding client
		// should finish noticeably before the waiting clients (which must poll
		// until cache is populated). If all clients finish in near-identical
		// time it indicates no serialization — each downloaded from blob
		// independently.
		var fastest, slowest float64
		for i, r := range results {
			log.Info("Job timing", "pod", r.Pod, "elapsed", r.ElapsedSec, "throughput", r.ThroughputMB)
			if i == 0 || r.ElapsedSec < fastest {
				fastest = r.ElapsedSec
			}
			if i == 0 || r.ElapsedSec > slowest {
				slowest = r.ElapsedSec
			}
		}
		if fastest > 0 {
			ratio := slowest / fastest
			log.Info("Timing spread", "fastest", fastest, "slowest", slowest, "ratio", fmt.Sprintf("%.2f", ratio))
			Expect(ratio).To(BeNumerically(">", 1.1),
				"All clients finished in near-identical time (ratio %.2f) — "+
					"expected timing spread indicating lock-holder finishes first "+
					"and waiting clients are serialized behind it", ratio)
		}
	})
})

// provisionColdReadTestBlob creates a small test blob in Azure storage.
func provisionColdReadTestBlob(ctx context.Context, k8sClient client.Client, account, container, image, wiClientID, wiSA string) {
	uploadScript, err := fixtures.ReadUploadBlob()
	Expect(err).NotTo(HaveOccurred(), "Failed to read upload to blob fixture")

	logs, err := job.RunPython(ctx, k8sClient, job.PythonConfig{
		Name:      "cold-read-provision",
		Namespace: namespace,
		Image:     image,
		Script:    string(uploadScript),
		Env: map[string]string{
			"BLOB_NAME": coldReadTestBlobName,
			"BLOB_SIZE": fmt.Sprintf("%d", coldReadTestBlobSize),
		},
		Labels:  map[string]string{coldReadCleanupLabel: "true"},
		Timeout: 5 * time.Minute,

		StorageAccount:           account,
		StorageContainer:         container,
		WorkloadIdentityClientID: wiClientID,
		WorkloadIdentitySA:       wiSA,
	})
	if logs != "" {
		log.Info("Provision job output", "logs", logs)
	}
	Expect(err).NotTo(HaveOccurred(), "Failed to provision test blob")
}

// buildColdReadJob creates a K8s Job that reads a blob through the DACS cache.
func buildColdReadJob(ctx context.Context, jobName, account, container, image, discoveryURL, wiClientID, wiSA string) *batchv1.Job {
	readScript, err := fixtures.ReadColdReadWorker()
	Expect(err).NotTo(HaveOccurred(), "Failed to read cold_read_worker.py")

	chunkSize := fmt.Sprintf("%d", coldReadCacheChunkMiB*1024*1024)
	j, err := job.BuildPython(ctx, job.PythonConfig{
		Name:      jobName,
		Namespace: namespace,
		Image:     image,
		Script:    string(readScript),
		Env: map[string]string{
			"BLOB_NAME":           coldReadTestBlobName,
			"EXPECTED_SIZE":       fmt.Sprintf("%d", coldReadTestBlobSize),
			"CACHE_DISCOVERY_URL": discoveryURL,
			"CACHE_SERVER_PORT":   "9065",
			"CACHE_CHUNK_SIZE":    chunkSize,
			// Enable lock-wait so clients wait for the first downloader
			// instead of bypassing the cache and fetching from blob directly.
			"SI_remoteDownloadOnlyWhenLockHeldEnabled": "true",
		},
		Labels:                   map[string]string{coldReadCleanupLabel: "true"},
		StorageAccount:           account,
		StorageContainer:         container,
		WorkloadIdentityClientID: wiClientID,
		WorkloadIdentitySA:       wiSA,
	})
	Expect(err).NotTo(HaveOccurred(), "Failed to build read job %s", jobName)
	return j
}

// extractColdReadResult parses the JSON result from a cold read job's pod logs.
func extractColdReadResult(ctx context.Context, jobName string) *coldReadJobResult {
	var podList corev1.PodList
	err := k8sClient.List(ctx, &podList,
		client.InNamespace(namespace),
		client.MatchingLabels{"app": jobName})
	Expect(err).NotTo(HaveOccurred(), "Failed to list pods for job %s", jobName)
	Expect(podList.Items).NotTo(BeEmpty(), "No pods found for job %s", jobName)

	logs, err := pod.GetLogs(ctx, podList.Items[0].Name, namespace)
	Expect(err).NotTo(HaveOccurred(), "Failed to get logs for job %s", jobName)

	const startMarker = "=== COLD_READ_RESULT_JSON ==="
	const endMarker = "=== END_COLD_READ_RESULT_JSON ==="
	startIdx := strings.Index(logs, startMarker)
	Expect(startIdx).NotTo(Equal(-1),
		"No result JSON marker in pod logs for job %s. Logs:\n%s", jobName, logs)
	startIdx += len(startMarker)
	endIdx := strings.Index(logs[startIdx:], endMarker)
	Expect(endIdx).NotTo(Equal(-1),
		"No end marker in pod logs for job %s", jobName)
	jsonStr := strings.TrimSpace(logs[startIdx : startIdx+endIdx])

	var result coldReadJobResult
	err = json.Unmarshal([]byte(jsonStr), &result)
	Expect(err).NotTo(HaveOccurred(),
		"Failed to parse result JSON for job %s: %s", jobName, jsonStr)
	return &result
}
