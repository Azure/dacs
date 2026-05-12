// Package aks provides utilities for discovering and interacting with
// Azure Kubernetes Service clusters during resilience tests.
package aks

import (
	"context"
	"encoding/json"
	"fmt"
	"os/exec"
	"sort"
	"strings"

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

// UpgradeCluster upgrades the AKS cluster to the specified Kubernetes version.
func UpgradeCluster(ctx context.Context, subscription, resourceGroup, clusterName, version string, log logr.Logger) error {
	log.Info("Upgrading AKS cluster", "cluster", clusterName, "targetVersion", version)
	cmd := exec.CommandContext(ctx,
		"az", "aks", "upgrade",
		"--subscription", subscription,
		"--name", clusterName,
		"--resource-group", resourceGroup,
		"--kubernetes-version", version,
		"--yes",
	)
	_, err := utils.Run(cmd)
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
