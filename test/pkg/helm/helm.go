// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package helm provides utilities for interacting with Helm releases.
package helm

import (
	"context"
	"encoding/json"
	"fmt"
	"os/exec"
	"strings"
	"time"

	"github.com/go-logr/logr"

	"github.com/Azure/dacs/test/pkg/utils"
)

const (
	// OldControllerReleaseName is the helm release name for the old cache-server chart.
	OldControllerReleaseName = "cache-server"
	// NewControllerReleaseName is the helm release name for the new dacs chart.
	NewControllerReleaseName = "dacs"
	// NewControllerNamespace is the namespace where the new controller is deployed.
	NewControllerNamespace = "cache-server"
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
	// Try new controller first (dacs in cache-server namespace)
	info := GetReleaseInfo(ctx, NewControllerNamespace, NewControllerReleaseName)
	if info.ChartVersion != "<unknown>" {
		return info
	}

	// Fall back to old controller (cache-server in cache namespace)
	return GetReleaseInfo(ctx, cacheNamespace, OldControllerReleaseName)
}

// RollbackToPrevious rolls a Helm release back to its previous revision and
// returns the wall-clock duration. Per `helm rollback --help`, passing
// revision 0 (or omitting it) selects the immediately prior release, which
// removes the need for callers to query helm history first.
func RollbackToPrevious(ctx context.Context, release, namespace string, log logr.Logger) (time.Duration, error) {
	log.Info("rolling back helm release to previous revision", "release", release, "namespace", namespace)
	start := time.Now()
	cmd := exec.CommandContext(ctx,
		"helm", "rollback", release, "0",
		"--namespace", namespace,
		"--wait",
		"--timeout", "10m",
	)
	output, err := utils.Run(cmd)
	duration := time.Since(start)
	if err != nil {
		return duration, fmt.Errorf("failed to rollback helm release %s in namespace %s to previous revision: %s: %w", release, namespace, strings.TrimSpace(output), err)
	}
	log.Info("helm rollback completed", "release", release, "namespace", namespace, "duration", duration.Seconds())
	return duration, nil
}

// Upgrade upgrades a Helm release and returns the wall-clock duration.
// If version is non-empty, --version is passed to helm (required for OCI chart refs).
func Upgrade(ctx context.Context, release, namespace, chartRef, version string, log logr.Logger) (time.Duration, error) {
	log.Info("upgrading helm release", "release", release, "namespace", namespace, "chartRef", chartRef, "version", version)
	start := time.Now()
	args := []string{
		"upgrade", release, chartRef,
		"--namespace", namespace,
		"--reset-then-reuse-values",
		"--atomic",
		"--wait",
		"--timeout", "10m",
	}
	if version != "" {
		args = append(args, "--version", version)
	}
	cmd := exec.CommandContext(ctx, "helm", args...)
	output, err := utils.Run(cmd)
	duration := time.Since(start)
	if err != nil {
		return duration, fmt.Errorf("failed to upgrade helm release %s in namespace %s: %s: %w", release, namespace, strings.TrimSpace(output), err)
	}
	log.Info("helm upgrade completed", "release", release, "namespace", namespace, "duration", duration.Seconds())
	return duration, nil
}
