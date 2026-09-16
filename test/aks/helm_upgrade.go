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

package aks

import (
	"context"
	"fmt"
	"os"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/common"
	helmhelpers "github.com/Azure/dacs/test/pkg/helm"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/verification"
)

const (
	helmUpgradeRelease = "dacs"

	// helmUpgradeChartRepoPath mirrors DACS_CACHE_CHART_REPO in deploy.mk
	// and is the path under HELM_UPGRADE_TARGET_REGISTRY where the chart is
	// pushed.
	helmUpgradeChartRepoPath = "charts/dacs"

	// opHelmUpgrade and opHelmRollback are the operation names used when
	// emitting per-operation perf metrics for this spec.
	opHelmUpgrade  = "helm-upgrade"
	opHelmRollback = "helm-rollback"
)

func testClusterHelmUpgrade(tc *testContext) {
	It("should survive a helm upgrade and rollback", Label("helm-upgrade"), func(ctx SpecContext) {
		registry := os.Getenv("HELM_UPGRADE_TARGET_REGISTRY")
		version := os.Getenv("HELM_UPGRADE_TARGET_VERSION")
		Expect(registry).NotTo(BeEmpty(), "HELM_UPGRADE_TARGET_REGISTRY must be set")
		Expect(version).NotTo(BeEmpty(), "HELM_UPGRADE_TARGET_VERSION must be set")
		chartRef := fmt.Sprintf("oci://%s/%s", registry, helmUpgradeChartRepoPath)
		helmUpgradeNamespace := common.GetNamespace()

		preUpgradeReleaseInfo := helmhelpers.GetReleaseInfo(ctx, helmUpgradeNamespace, helmUpgradeRelease)
		Expect(preUpgradeReleaseInfo.AppVersion).NotTo(Or(BeEmpty(), Equal("<unknown>")), "helm release %s/%s has no usable AppVersion: %q", helmUpgradeNamespace, helmUpgradeRelease, preUpgradeReleaseInfo.AppVersion)

		preUpgradeImage, err := resolveCacheServerImage(ctx, helmUpgradeNamespace, helmUpgradeRelease, preUpgradeReleaseInfo.AppVersion)
		Expect(err).NotTo(HaveOccurred(), "failed to resolve pre-upgrade cacheserver image")
		log.Info("resolved helm upgrade target", "chartRef", chartRef, "version", version, "registry", registry, "preUpgradeImage", preUpgradeImage)

		By(fmt.Sprintf("performing helm upgrade to %s version=%q", chartRef, version))
		upgradeDuration, err := helmhelpers.Upgrade(ctx, helmUpgradeRelease, helmUpgradeNamespace, chartRef, version, log)
		Expect(err).NotTo(HaveOccurred(), "failed to upgrade helm release %s/%s to %s %s", helmUpgradeNamespace, helmUpgradeRelease, chartRef, version)
		log.Info("helm upgrade completed", "release", helmUpgradeRelease, "namespace", helmUpgradeNamespace, "helmUpgradeDuration", upgradeDuration.Seconds())
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opHelmUpgrade, Details: kusto.UpgradeOperationDetails{TargetVersion: version}}, upgradeDuration)

		postUpgradeReleaseInfo := helmhelpers.GetReleaseInfo(ctx, helmUpgradeNamespace, helmUpgradeRelease)
		Expect(postUpgradeReleaseInfo.AppVersion).NotTo(Or(BeEmpty(), Equal("<unknown>")), "post-upgrade helm release %s/%s has no usable AppVersion: %q", helmUpgradeNamespace, helmUpgradeRelease, postUpgradeReleaseInfo.AppVersion)
		postUpgradeImage, err := resolveCacheServerImage(ctx, helmUpgradeNamespace, helmUpgradeRelease, postUpgradeReleaseInfo.AppVersion)
		Expect(err).NotTo(HaveOccurred(), "failed to resolve post-upgrade cacheserver image")
		log.Info("resolved expected post-upgrade cacheserver image", "image", postUpgradeImage)

		By("waiting for application health and cacheserver image rollout after upgrade")
		Eventually(func(g Gomega, ctx context.Context) {
			verification.VerifyAppHealthWithImage(g, ctx, tc.Client, tc.namespace, postUpgradeImage, log)
		}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).
			Should(Succeed(), "application health verification failed after helm upgrade")

		By("verifying cache serving after upgrade")
		Expect(verification.VerifyCacheServing(ctx, tc.Client, tc.namespace, log)).To(Succeed(), "cache serving verification failed after helm upgrade")

		// `helm rollback <release> 0` rolls back to the previous release, so
		// we don't need to track or look up the baseline revision number.
		By("performing helm rollback to the previous revision")
		rollbackDuration, err := helmhelpers.RollbackToPrevious(ctx, helmUpgradeRelease, helmUpgradeNamespace, log)
		Expect(err).NotTo(HaveOccurred(), "failed to rollback helm release %s/%s to previous revision", helmUpgradeNamespace, helmUpgradeRelease)
		log.Info("helm rollback completed", "release", helmUpgradeRelease, "namespace", helmUpgradeNamespace, "helmRollbackDuration", rollbackDuration.Seconds())
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opHelmRollback}, rollbackDuration)

		By("waiting for application health and cacheserver image rollback after rollback")
		Eventually(func(g Gomega, ctx context.Context) {
			verification.VerifyAppHealthWithImage(g, ctx, tc.Client, tc.namespace, preUpgradeImage, log)
		}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).
			Should(Succeed(), "application health verification failed after helm rollback")

		// Cache serving + infra/app health after rollback are also covered
		// by the shared afterEach in aks.go
	})
}

// resolveCacheServerImage returns the cacheserver image the release will
// actually run, derived from the release's effective (merged) Helm values
// rather than assumed from the chart version. --reset-then-reuse-values
// preserves a previously pinned manager.cacheServerImage.tag across an
// upgrade, so the chart version alone cannot predict the running image.
func resolveCacheServerImage(ctx context.Context, namespace, release, appVersion string) (string, error) {
	values, err := helmhelpers.GetValues(ctx, release, namespace)
	if err != nil {
		return "", err
	}
	manager, _ := values["manager"].(map[string]any)
	image, _ := manager["cacheServerImage"].(map[string]any)
	repository, _ := image["repository"].(string)
	if repository == "" {
		return "", fmt.Errorf("manager.cacheServerImage.repository not found in helm values for release %s/%s", namespace, release)
	}
	tag, _ := image["tag"].(string)
	if tag == "" {
		// Chart defaults an empty tag to .Chart.AppVersion at render time.
		tag = appVersion
	}
	return fmt.Sprintf("%s:%s", repository, tag), nil
}
