/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Cache server Prometheus metrics scraping for scenario test verification
# --------------------------------------------------------------------------------------------------------------------
*/
package scenario

import (
	"context"
	"fmt"
	"os/exec"
	"strconv"
	"strings"

	"github.com/Azure/dacs/test/pkg/utils"
)

// cacheServerMetrics holds aggregated download request counters from all cache servers.
type cacheServerMetrics struct {
	// downloadSuccess counts Download requests with status=Success (cache hits).
	downloadSuccess int
	// downloadInvalidTransition counts Download requests with status=InvalidTransition (cache misses).
	downloadInvalidTransition int
}

// scrapeCacheServerMetrics scrapes Prometheus metrics from all cacheserver pods and returns
// aggregated download request counters. This is used to compute cache hit/miss deltas
// around test executions.
func scrapeCacheServerMetrics(ctx context.Context, namespace string) cacheServerMetrics {
	// Find all cacheserver pods
	cmd := exec.CommandContext(ctx, "kubectl", "get", "pods", "-n", namespace,
		"-l", "app=cacheserver",
		"-o", "jsonpath={range .items[*]}{.metadata.name}{\"\\n\"}{end}")
	output, err := utils.Run(cmd)
	if err != nil {
		log.Info("Warning: failed to list cacheserver pods for metrics", "error", err)
		return cacheServerMetrics{}
	}

	var total cacheServerMetrics
	for _, podName := range strings.Split(strings.TrimSpace(output), "\n") {
		if podName == "" {
			continue
		}
		m := scrapeSingleCacheServer(ctx, podName, namespace)
		total.downloadSuccess += m.downloadSuccess
		total.downloadInvalidTransition += m.downloadInvalidTransition
	}
	return total
}

// scrapeSingleCacheServer scrapes Prometheus metrics from a single cache server pod.
func scrapeSingleCacheServer(ctx context.Context, podName, namespace string) cacheServerMetrics {
	cmd := exec.CommandContext(ctx, "kubectl", "exec", "-n", namespace, podName,
		"-c", "cacheserver", "--",
		"curl", "-s", "http://localhost:9096/metrics")
	output, err := utils.Run(cmd)
	if err != nil {
		log.Info("Warning: failed to scrape metrics", "pod", podName, "error", err)
		return cacheServerMetrics{}
	}

	var m cacheServerMetrics
	for _, line := range strings.Split(output, "\n") {
		if strings.HasPrefix(line, "#") || line == "" {
			continue
		}
		if strings.Contains(line, "cache_server_request_counter") &&
			strings.Contains(line, "request_type=\"Download\"") {
			val := parsePrometheusValue(line)
			if strings.Contains(line, "status=\"Success\"") {
				m.downloadSuccess += val
			} else if strings.Contains(line, "status=\"InvalidTransition\"") {
				m.downloadInvalidTransition += val
			}
		}
	}
	return m
}

// computeCacheMetricsDelta computes the change in download hits and misses between two snapshots.
// In the cache server protocol:
//   - Download+Success = cache hit (data found in cache, returned to client)
//   - Download+InvalidTransition = cache miss (data not found, client must upload)
func computeCacheMetricsDelta(before, after cacheServerMetrics) (hits, misses int) {
	hits = after.downloadSuccess - before.downloadSuccess
	misses = after.downloadInvalidTransition - before.downloadInvalidTransition
	return
}

func parsePrometheusValue(line string) int {
	parts := strings.Fields(line)
	if len(parts) < 2 {
		return 0
	}
	// Prometheus counters may have decimal points (e.g., "773.0" or "773")
	valStr := parts[len(parts)-1]
	if f, err := strconv.ParseFloat(valStr, 64); err == nil {
		return int(f)
	}
	if v, err := strconv.Atoi(valStr); err == nil {
		return v
	}
	return 0
}

// FormatCacheMetricsSummary returns a human-readable summary of cache hit/miss counts.
func FormatCacheMetricsSummary(hits, misses int) string {
	total := hits + misses
	if total == 0 {
		return "no cache downloads (expected for no_cache mode)"
	}
	rate := float64(hits) / float64(total) * 100
	return fmt.Sprintf("hits=%d misses=%d total=%d hitRate=%.1f%%", hits, misses, total, rate)
}
