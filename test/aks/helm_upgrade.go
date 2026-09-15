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
	helmUpgradeChartRepoPath = "helmchart/dacs"

	// cacheServerImageRepoName is the repository segment of the cacheserver
	// image as published into the target ACR. It must match the
	// `manager.cacheServerImage.repository` rewrite in
	// .pipelines/scripts/publish/publish-helm-chart.sh, which sets it to
	// "<registry>/cache-server".
	cacheServerImageRepoName = "cache-server"

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

		preUpgradeImage := expectedCacheServerImage(registry, preUpgradeReleaseInfo.AppVersion)
		log.Info("resolved helm upgrade target", "chartRef", chartRef, "version", version, "registry", registry, "preUpgradeImage", preUpgradeImage)

		By(fmt.Sprintf("performing helm upgrade to %s version=%q", chartRef, version))
		upgradeDuration, err := helmhelpers.Upgrade(ctx, helmUpgradeRelease, helmUpgradeNamespace, chartRef, version, log)
		Expect(err).NotTo(HaveOccurred(), "failed to upgrade helm release %s/%s to %s %s", helmUpgradeNamespace, helmUpgradeRelease, chartRef, version)
		log.Info("helm upgrade completed", "release", helmUpgradeRelease, "namespace", helmUpgradeNamespace, "helmUpgradeDuration", upgradeDuration.Seconds())
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opHelmUpgrade, Details: kusto.UpgradeOperationDetails{TargetVersion: version}}, upgradeDuration)

		postUpgradeReleaseInfo := helmhelpers.GetReleaseInfo(ctx, helmUpgradeNamespace, helmUpgradeRelease)
		Expect(postUpgradeReleaseInfo.AppVersion).NotTo(Or(BeEmpty(), Equal("<unknown>")), "post-upgrade helm release %s/%s has no usable AppVersion: %q", helmUpgradeNamespace, helmUpgradeRelease, postUpgradeReleaseInfo.AppVersion)
		postUpgradeImage := expectedCacheServerImage(registry, postUpgradeReleaseInfo.AppVersion)
		log.Info("computed expected post-upgrade cacheserver image", "image", postUpgradeImage)

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

// expectedCacheServerImage returns the <registry>/cache-server:<tag> the
// cacheserver StatefulSet should be running given the registry the chart
// was published to and the chart's helm AppVersion.
func expectedCacheServerImage(registry, appVersion string) string {
	return fmt.Sprintf("%s/%s:%s", registry, cacheServerImageRepoName, appVersion)
}
