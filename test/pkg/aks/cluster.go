// Package aks provides utilities for discovering and interacting with
// Azure Kubernetes Service clusters during resilience tests.
package aks

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os/exec"
	"sort"
	"strings"
	"time"

	"github.com/go-logr/logr"
	"golang.org/x/mod/semver"

	"github.com/Azure/dacs/test/pkg/utils"
)

// Azure resource tags used to discover the managed AKS cluster from a
// node resource group.
const (
	aksManagedRgTag      = "aks-managed-cluster-rg"
	aksManagedClusterTag = "aks-managed-cluster-name"
)

// ManagedClusterInfo holds the name and resource group of a managed AKS cluster
// discovered from Azure resource tags.
type ManagedClusterInfo struct {
	Name          string
	ResourceGroup string
}

// GetNodeResourceGroup extracts the node resource group name from a provider ID.
// The provider ID has the form:
//
//	azure:///subscriptions/<sub>/resourceGroups/<rg>/providers/...
func GetNodeResourceGroup(providerID string) (string, error) {
	return getSegmentAfterKey(providerID, "resourceGroups")
}

// GetSubscription extracts the Azure subscription ID from a provider ID.
func GetSubscription(providerID string) (string, error) {
	return getSegmentAfterKey(providerID, "subscriptions")
}

// getSegmentAfterKey splits a provider ID on "/" and returns the segment
// immediately following the given key.
func getSegmentAfterKey(providerID, key string) (string, error) {
	parts := strings.Split(providerID, "/")
	for i, part := range parts {
		if strings.EqualFold(part, key) && i+1 < len(parts) {
			return parts[i+1], nil
		}
	}
	return "", fmt.Errorf("could not find %q in providerID %s", key, providerID)
}

// GetManagedClusterInfo discovers the AKS managed cluster name and resource
// group by querying Azure tags on the node resource group.
func GetManagedClusterInfo(ctx context.Context, subscription, nodeResourceGroup string, log logr.Logger) (ManagedClusterInfo, error) {
	cmd := exec.CommandContext(ctx,
		"az", "group", "show",
		"--subscription", subscription,
		"--name", nodeResourceGroup,
		"--query", "tags",
		"-o", "json",
	)
	output, err := utils.Run(cmd)
	if err != nil {
		return ManagedClusterInfo{}, fmt.Errorf("failed to query tags for resource group %s: %w", nodeResourceGroup, err)
	}

	var tags map[string]string
	if err := json.Unmarshal([]byte(output), &tags); err != nil {
		return ManagedClusterInfo{}, fmt.Errorf("failed to parse tags JSON: %w", err)
	}

	clusterName, ok := tags[aksManagedClusterTag]
	if !ok || clusterName == "" {
		return ManagedClusterInfo{}, fmt.Errorf("tag %q not found on resource group %s", aksManagedClusterTag, nodeResourceGroup)
	}
	resourceGroup, ok := tags[aksManagedRgTag]
	if !ok || resourceGroup == "" {
		return ManagedClusterInfo{}, fmt.Errorf("tag %q not found on resource group %s", aksManagedRgTag, nodeResourceGroup)
	}

	log.Info("Discovered managed cluster", "name", clusterName, "resourceGroup", resourceGroup)
	return ManagedClusterInfo{Name: clusterName, ResourceGroup: resourceGroup}, nil
}

// GetAvailableUpgrades returns the list of available Kubernetes versions that
// the cluster can be upgraded to.
func GetAvailableUpgrades(ctx context.Context, subscription, resourceGroup, clusterName string) ([]string, error) {
	cmd := exec.CommandContext(ctx,
		"az", "aks", "get-upgrades",
		"--subscription", subscription,
		"--name", clusterName,
		"--resource-group", resourceGroup,
		"-o", "tsv",
		"--query", "controlPlaneProfile.upgrades[*].kubernetesVersion",
	)
	output, err := utils.Run(cmd)
	if err != nil {
		return nil, fmt.Errorf("failed to get available upgrades for cluster %s: %w", clusterName, err)
	}
	return utils.GetNonEmptyLines(strings.TrimSpace(output)), nil
}

// GetDefaultNodePool returns the system node pool for an AKS cluster, or the
// first node pool if no system node pool is present.
func GetDefaultNodePool(ctx context.Context, subscription, resourceGroup, clusterName string) (string, error) {
	cmd := exec.CommandContext(ctx,
		"az", "aks", "nodepool", "list",
		"--subscription", subscription,
		"--resource-group", resourceGroup,
		"--cluster-name", clusterName,
		"-o", "json",
	)
	output, err := utils.Run(cmd)
	if err != nil {
		return "", fmt.Errorf("failed to list node pools for cluster %s: %w", clusterName, err)
	}

	var nodePools []struct {
		Name string `json:"name"`
		Mode string `json:"mode"`
	}
	if err := json.Unmarshal([]byte(output), &nodePools); err != nil {
		return "", fmt.Errorf("failed to parse node pool JSON for cluster %s: %w", clusterName, err)
	}
	if len(nodePools) == 0 {
		return "", fmt.Errorf("no node pools found for cluster %s", clusterName)
	}
	for _, nodePool := range nodePools {
		if strings.EqualFold(nodePool.Mode, "System") {
			return nodePool.Name, nil
		}
	}
	return nodePools[0].Name, nil
}

// ErrCapacity is returned by az control-plane operations (node pool scale,
// cluster upgrade) when Azure cannot allocate the requested compute because of
// a capacity/allocation constraint (for example OverconstrainedAllocationRequest
// or SkuNotAvailable). Callers should treat this as an environmental skip rather
// than a product or test failure. The wrapped error carries the operation
// specific detail (which node pool / which upgrade).
var ErrCapacity = errors.New("az operation failed due to Azure capacity constraints")

const (
	// azMaxAttempts is the total number of times a retryable az control-plane
	// command (nodepool scale, cluster upgrade) is invoked before giving up.
	azMaxAttempts = 3

	// azRetryDelay is the base backoff between retry attempts. The delay grows
	// linearly with the attempt number.
	azRetryDelay = 30 * time.Second
)

// capacityErrorMarkers are substrings (matched case-insensitively) in the az
// output that indicate an allocation/capacity constraint. These are
// environmental: we deliberately do NOT retry them and instead surface them as
// skippable. Any other failure is assumed transient and retried.
var capacityErrorMarkers = []string{
	"overconstrainedallocationrequest",
	"allocationfailed",
	"allocation failed",
	"zonalallocationfailed",
	"skunotavailable",
	"outofcapacity",
	"insufficientcapacity",
	"quotaexceeded",
}

// isCapacityError reports whether the combined az stdout/stderr indicates an
// allocation/capacity constraint that should not be retried.
func isCapacityError(output string) bool {
	lower := strings.ToLower(output)
	for _, m := range capacityErrorMarkers {
		if strings.Contains(lower, m) {
			return true
		}
	}
	return false
}

// retryAzCommand runs an az control-plane command with retry and linear
// backoff. buildCmd is called once per attempt so each invocation gets a fresh
// *exec.Cmd. On a capacity/allocation constraint the command is not retried;
// instead the underlying error is returned wrapping ErrCapacity so the caller
// can skip. Any other failure is assumed transient and retried up to
// azMaxAttempts times. The caller is expected to wrap the returned error with
// operation-specific context.
func retryAzCommand(ctx context.Context, log logr.Logger, opName string, buildCmd func() *exec.Cmd) error {
	var lastErr error
	for attempt := 1; attempt <= azMaxAttempts; attempt++ {
		output, err := utils.Run(buildCmd())
		if err == nil {
			return nil
		}
		lastErr = err

		// Capacity constraints are environmental and not worth retrying.
		if isCapacityError(output) {
			log.Info("az command hit an Azure capacity constraint; surfacing as skippable",
				"op", opName, "attempt", attempt)
			return fmt.Errorf("%w: %w", ErrCapacity, err)
		}

		// Everything else is assumed transient: retry with linear backoff.
		if attempt == azMaxAttempts {
			return lastErr
		}
		delay := azRetryDelay * time.Duration(attempt)
		log.Info("az command failed with a retryable error; retrying after backoff",
			"op", opName, "attempt", attempt, "maxAttempts", azMaxAttempts, "retryDelay", delay.String())
		select {
		case <-ctx.Done():
			return fmt.Errorf("%w (context canceled during retry backoff: %w)", lastErr, ctx.Err())
		case <-time.After(delay):
		}
	}
	return lastErr
}

// ScaleNodePool scales an AKS node pool to the requested node count. Any
// failure other than an Azure capacity constraint is assumed transient and
// retried with linear backoff. When Azure cannot allocate the requested
// capacity, it returns an error wrapping ErrCapacity so callers can
// skip rather than fail.
func ScaleNodePool(ctx context.Context, subscription, resourceGroup, clusterName, nodepool string, count int, log logr.Logger) error {
	log.Info("Scaling AKS nodepool", "cluster", clusterName, "nodepool", nodepool, "count", count)

	err := retryAzCommand(ctx, log, "nodepool-scale", func() *exec.Cmd {
		return exec.CommandContext(ctx,
			"az", "aks", "nodepool", "scale",
			"--subscription", subscription,
			"--resource-group", resourceGroup,
			"--cluster-name", clusterName,
			"--name", nodepool,
			"--node-count", fmt.Sprintf("%d", count),
		)
	})
	if err != nil {
		return fmt.Errorf("failed to scale node pool %s for cluster %s to %d nodes: %w", nodepool, clusterName, count, err)
	}
	log.Info("AKS nodepool scale completed", "cluster", clusterName, "nodepool", nodepool, "count", count)
	return nil
}

// UpgradeCluster upgrades the AKS cluster to the specified Kubernetes version.
// A rolling upgrade provisions a surge node, so it can hit the same Azure
// capacity constraints as a node pool scale-up. Any failure other than a
// capacity constraint is assumed transient and retried with linear backoff;
// capacity constraints are returned wrapping ErrCapacity so callers can skip
// rather than fail.
func UpgradeCluster(ctx context.Context, subscription, resourceGroup, clusterName, version string, log logr.Logger) error {
	log.Info("Upgrading AKS cluster", "cluster", clusterName, "targetVersion", version)

	err := retryAzCommand(ctx, log, "cluster-upgrade", func() *exec.Cmd {
		return exec.CommandContext(ctx,
			"az", "aks", "upgrade",
			"--subscription", subscription,
			"--name", clusterName,
			"--resource-group", resourceGroup,
			"--kubernetes-version", version,
			"--yes",
		)
	})
	if err != nil {
		return fmt.Errorf("failed to upgrade cluster %s to version %s: %w", clusterName, version, err)
	}
	log.Info("Cluster upgrade completed", "cluster", clusterName, "version", version)
	return nil
}

// GetNextSemver selects the lowest available upgrade version from a list of
// Kubernetes version strings (e.g. "1.30.1", "1.31.0"). It adds the "v"
// prefix required by golang.org/x/mod/semver, sorts, picks the lowest valid
// entry, and strips the prefix before returning.
func GetNextSemver(versions []string) (string, error) {
	var valid []string
	for _, v := range versions {
		sv := "v" + v
		if semver.IsValid(sv) {
			valid = append(valid, sv)
		}
	}
	if len(valid) == 0 {
		return "", fmt.Errorf("no valid semver versions found in %v", versions)
	}
	sort.Slice(valid, func(i, j int) bool {
		return semver.Compare(valid[i], valid[j]) < 0
	})
	result, found := strings.CutPrefix(valid[0], "v")
	if !found {
		return "", fmt.Errorf("failed to strip 'v' prefix from version %s", valid[0])
	}
	return result, nil
}
