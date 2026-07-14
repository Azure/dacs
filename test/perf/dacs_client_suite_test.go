/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
# End to End test for multiple cache clients performing writes
# --------------------------------------------------------------------------------------------------------------------
*/
package perf

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/utils"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/client-go/tools/clientcmd"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/config"
)

var testMultipleDACSClients = func(ctx context.Context) {
	var count int
	flag.Parse()
	err := validateWriteMode(opts.WriteMode)
	Expect(err).ShouldNot(HaveOccurred(), "Invalid write mode provided")

	opts.ResolveDefaults(ctx)

	// Build K8s client
	k8sClient := getK8sClientOrDie()
	// TODO: Re-enable dynamic container creation after fixing az storage CLI access from pipeline agents.
	// now := time.Now().Format(time.RFC3339)
	// now = strings.ReplaceAll(strings.ToLower(now), ":", "-")
	// containerName := containerNamePrefix + now

	// Get no.of clients to be deployed based on k8s nodes
	if opts.ClientCount > 0 {
		log.Info("Using user provided cache client count: %d", opts.ClientCount)
		count = int(opts.ClientCount)
	} else {
		nodeList, err := common.GetNodeList(ctx, k8sClient)
		Expect(err).ShouldNot(HaveOccurred(), "Failed to get node list from cluster")
		count = len(nodeList.Items)
	}

	config := &CacheClientConfig{
		k8sClient:                k8sClient,
		workloadIdentityClientID: opts.WorkloadIdentityClientID,
		serviceAccountName:       opts.WorkloadIdentitySA,
		count:                    count,
		cacheServerNamespace:     common.GetNamespace(),
		cacheClientImage:         getClientImage(),
		storageAccountName:       opts.StorageAccount,
		containerName:            opts.StorageContainer,
		blobCount:                opts.BlobCount,
		blobSize:                 opts.BlobSize,
		writeMode:                opts.WriteMode,
	}

	config.dacsClientTestSuite(ctx)
}

var _ = Describe("Multiple DACS Clients E2E Tests", Label("dacsclients", "perf", "aks"), Ordered, func() {
	Context("Multiple DACS Clients Test Suite", Label("dacsclients"), func() {
		BeforeAll(func(ctx context.Context) {
			Eventually(func(g Gomega) {
				// Wait for pods to come into running state
				cmd := exec.CommandContext(ctx, "kubectl", "get", "pod", "-o", "jsonpath='{.items[*].metadata.name},'", "-n", "mpi-operator")
				output, err := utils.RunOutput(cmd)
				g.Expect(err).ShouldNot(HaveOccurred(), "Failed to get mpi-operator pods")
				podNames := strings.Split(strings.Trim(output, "'"), ",")
				for _, podName := range podNames {
					if strings.TrimSpace(podName) == "" {
						continue
					}
					phase, err := pod.GetPhase(ctx, podName, "mpi-operator")
					g.Expect(err).ShouldNot(HaveOccurred(), "Failed to get phase for pod %s in mpi-operator namespace", podName)
					g.Expect(phase).To(Equal("Running"), "Pod %s in mpi-operator namespace is not running", podName)
				}
			}).WithContext(ctx).WithTimeout(defaultWaitTime).WithPolling(5*time.Second).Should(Succeed(), "mpi-operator pods should come to running state")

			Eventually(func(g Gomega) {
				// Create namespace for cacheclients
				cmd := exec.CommandContext(ctx, "kubectl", "get", "ns", cacheClientNs)
				_, err := utils.Run(cmd)
				g.Expect(err).ShouldNot(HaveOccurred(), "Failed to check existence of %s namespace", cacheClientNs)
			}).WithContext(ctx).Should(Succeed(), "Namespace creation failed or namespace not found")
		})

		It("Should run multiple dacs cache clients performing writes", testMultipleDACSClients)
	})
})

func validateWriteMode(mode string) error {
	if _, exists := utils.WriteSupportModes[mode]; !exists {
		return fmt.Errorf("unsupported write mode: %s", mode)
	}
	return nil
}

func getK8sClientOrDie() client.Client {
	if os.Getenv(clientcmd.RecommendedConfigPathEnvVar) == "" {
		err := os.Setenv(clientcmd.RecommendedConfigPathEnvVar, defaultKubeConfigPath)
		if err != nil {
			log.Error(err, "Failed to set default kubeconfig path")
			os.Exit(1)
		}
	}
	// Set up controller-runtime client
	cfg, err := config.GetConfig()
	if err != nil {
		log.Error(err, "Failed to get kubeconfig")
		os.Exit(1)
	}
	k8sClient, err := client.New(cfg, client.Options{})
	if err != nil {
		log.Error(err, "Failed to create controller-runtime client")
		os.Exit(1)
	}
	return k8sClient
}
