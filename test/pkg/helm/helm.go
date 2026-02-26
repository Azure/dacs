// Package helm provides utilities for interacting with Helm releases.
package helm

import (
	"context"
	"encoding/json"
	"os/exec"

	"github.com/Azure/dacs/test/pkg/utils"
)

const (
	// OldControllerReleaseName is the helm release name for the old cache-server chart.
	OldControllerReleaseName = "cache-server"
	// NewControllerReleaseName is the helm release name for the new dacs chart.
	NewControllerReleaseName = "dacs"
	// NewControllerNamespace is the namespace where the new controller is deployed.
	NewControllerNamespace = "cache-controller-system"
)

// Info contains helm chart version information.
type Info struct {
	ChartVersion string
	AppVersion   string
}

// helmRelease represents a helm release from `helm list` output.
type helmRelease struct {
	Name       string `json:"name"`
	Chart      string `json:"chart"`
	AppVersion string `json:"app_version"`
}

// GetReleaseInfo gets the version info of a deployed helm chart by release name.
func GetReleaseInfo(ctx context.Context, namespace, releaseName string) Info {
	cmd := exec.CommandContext(ctx, "helm", "list", "-n", namespace, "-o", "json")
	output, err := utils.RunOutput(cmd)
	if err != nil {
		return Info{ChartVersion: "<unknown>", AppVersion: "<unknown>"}
	}

	// Parse JSON to extract chart version
	// Output is like: [{"name":"cache-server","chart":"cache-server-0.1.0","app_version":"1.0.0",...}]
	var releases []helmRelease
	if err := json.Unmarshal([]byte(output), &releases); err != nil {
		return Info{ChartVersion: "<unknown>", AppVersion: "<unknown>"}
	}

	for _, r := range releases {
		if r.Name == releaseName {
			chartVersion := r.Chart
			if chartVersion == "" {
				chartVersion = "<unknown>"
			}
			appVersion := r.AppVersion
			if appVersion == "" {
				appVersion = "<unknown>"
			}
			return Info{ChartVersion: chartVersion, AppVersion: appVersion}
		}
	}

	return Info{ChartVersion: "<unknown>", AppVersion: "<unknown>"}
}

// GetCacheReleaseInfo returns helm info for the deployed cache system.
// It checks for the new dacs release first, then falls back to the old cache-server release.
func GetCacheReleaseInfo(ctx context.Context, cacheNamespace string) Info {
	// Try new controller first (dacs in cache-controller-system namespace)
	info := GetReleaseInfo(ctx, NewControllerNamespace, NewControllerReleaseName)
	if info.ChartVersion != "<unknown>" {
		return info
	}

	// Fall back to old controller (cache-server in cache namespace)
	return GetReleaseInfo(ctx, cacheNamespace, OldControllerReleaseName)
}
