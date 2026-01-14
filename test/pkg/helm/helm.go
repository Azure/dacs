// Package helm provides utilities for interacting with Helm releases.
package helm

import (
	"context"
	"encoding/json"
	"os/exec"

	"github.com/Azure/dacs/test/pkg/utils"
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
