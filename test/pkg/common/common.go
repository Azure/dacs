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

package common

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	"github.com/onsi/ginkgo/v2/reporters"
	"github.com/onsi/ginkgo/v2/types"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/kube/secret"
	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
	"sigs.k8s.io/yaml"
)

const (
	// trueString is the string representation of a boolean true value.
	trueString = "true"

	controllerNamespace      = "cache-server"
	controllerDeploymentName = "dacs-manager"
	cacheControllerCRDName   = "caches.storage.azure.com"

	// LegacyStatefulSetName is the hardcoded StatefulSet name used by the
	// old helm/cache-server chart (no controller). The StatefulSet was always
	// named "cacheserver" regardless of the release name.
	LegacyStatefulSetName = "cacheserver"

	// ControllerCacheName is the default Cache CR name created by the
	// dacs controller chart (values.cache.name). The controller
	// names the StatefulSet after the Cache CR, so this is also the
	// StatefulSet name when the controller is managing the deployment.
	ControllerCacheName = "cache-sample"
)

// log is the default logger for the test package. It uses klog's textlogger
// to output in the standard klog format, writing to GinkgoWriter.
var log = logger.New()

var (
	// Optional Environment Variables:
	// - INSTALL=true: Install DACS components before tests. Default is to
	//   skip installation (assumes components are already deployed).
	// - INSTALL_PREREQ=true: Install dacs-prereq before tests. Default
	//   is to skip installation.
	// - UNINSTALL=true: Uninstall DACS components after tests. Default is
	//   to skip uninstallation.
	// - UNINSTALL_PREREQ=true: Uninstall dacs-prereq after tests.
	//   Default is to skip uninstallation.
	// - SKIP_SUPPORT_BUNDLE=true: Skips collecting support bundle after the
	//   test.
	doInstall         = os.Getenv("INSTALL") == trueString
	doInstallPrereq   = os.Getenv("INSTALL_PREREQ") == trueString
	doUninstall       = os.Getenv("UNINSTALL") == trueString
	doUninstallPrereq = os.Getenv("UNINSTALL_PREREQ") == trueString
	skipSupportBundle = os.Getenv("SKIP_SUPPORT_BUNDLE") == trueString

	startTime     = time.Now()
	filePathRegex = regexp.MustCompile(`[ /]`)
)

// GetNamespace returns the namespace from environment or default.
func GetNamespace() string {
	if ns := os.Getenv("CACHE_NAMESPACE"); ns != "" {
		return ns
	}
	return DEFAULT_CACHESERVER_NAMESPACE
}

// GetDefaultImage returns the default perf client image based on environment variables
// matching the helm chart configuration. On minikube, uses local image.
func GetDefaultImage() string {
	// On minikube, use the local image that was loaded via `minikube image load`
	if cluster.IsMinikube() {
		return "cache-test-client:local"
	}

	registry := os.Getenv("DOCKER_REGISTRY")
	if registry == "" {
		registry = "docker.io"
	}
	buildNumber := os.Getenv("BUILD_NUMBER")
	if buildNumber == "" {
		buildNumber = "latest"
	}

	return fmt.Sprintf("%s/cache-test-client:%s-linux-amd64", registry, buildNumber)
}

// Setup sets up the test environment for DACS E2E tests.
func Setup(ctx context.Context, k8sClient client.Client, namespace string) {
	cmd := exec.CommandContext(ctx, "az", "--version")
	output, err := utils.Run(cmd)
	Expect(err).NotTo(HaveOccurred(), "Failed to get az cli version")
	log.Info("az cli version", "version", output)

	cmd = exec.CommandContext(ctx, "kubectl", "version")
	output, err = utils.Run(cmd)
	Expect(err).NotTo(HaveOccurred(), "Failed to get kubectl version")
	log.Info("kubectl version", "version", output)

	if !doInstall {
		log.Info("Skipping installation (set INSTALL=true to install)")
		controllerPresent := controllerDeploymentExists(ctx, k8sClient)
		if controllerPresent {
			ensureControllerInstall(ctx, k8sClient)
		}
		// The new controller names the StatefulSet after the Cache CR;
		// the legacy helm chart always used "cacheserver".
		stsName := LegacyStatefulSetName
		if controllerPresent {
			stsName = ControllerCacheName
		}
		Eventually(func() bool {
			return statefulSetExists(ctx, k8sClient, namespace, stsName)
		}).WithTimeout(5*time.Minute).WithPolling(5*time.Second).Should(BeTrue(),
			fmt.Sprintf("Cache StatefulSet %q not found. Ensure cache infrastructure is installed before running tests.", stsName))
		ensureCacheReady(ctx, k8sClient, namespace, stsName, controllerPresent)
		return
	}

	By("creating namespace")
	Eventually(func(g Gomega) {
		ns := &corev1.Namespace{}
		err := k8sClient.Get(ctx, client.ObjectKey{Name: namespace}, ns)
		if apierrors.IsNotFound(err) {
			ns = &corev1.Namespace{
				ObjectMeta: metav1.ObjectMeta{
					Name: namespace,
				},
			}
			err := k8sClient.Create(ctx, ns)
			g.Expect(err).NotTo(HaveOccurred(), "Failed to create namespace")
		} else {
			g.Expect(err).NotTo(HaveOccurred(), "Failed to get namespace")
		}
	}).WithContext(ctx).Should(Succeed(), "Namespace creation failed or namespace not found")

	if doInstallPrereq {
		By("deploying cache server prerequisites")
		cmd = exec.CommandContext(ctx, "make", "helm-prereq-install")
		_, err = utils.Run(cmd)
		Expect(err).NotTo(HaveOccurred(), "Failed to deploy cache server prerequisites")
	} else {
		log.Info("Skipping dacs-prereq installation (set INSTALL_PREREQ=true to install)")
	}

	By("deploying cache server")
	Eventually(func(g Gomega, ctx context.Context) {
		cmd = exec.CommandContext(ctx, "make", "helm-install")
		_, err = utils.Run(cmd)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to deploy cache server")
	}).WithTimeout(5*time.Minute).WithContext(ctx).Should(Succeed(), "Failed to deploy cache server")

	ensureCacheReady(ctx, k8sClient, namespace, LegacyStatefulSetName, false)
}

func controllerDeploymentExists(ctx context.Context, k8sClient client.Client) bool {
	deploy := &appsv1.Deployment{}
	err := k8sClient.Get(ctx, client.ObjectKey{
		Name:      controllerDeploymentName,
		Namespace: controllerNamespace,
	}, deploy)

	if err == nil {
		return true
	}

	if !apierrors.IsNotFound(err) {
		log.Info("Unable to query dacs controller deployment", "error", err)
	}

	return false
}

func statefulSetExists(ctx context.Context, k8sClient client.Client, namespace, name string) bool {
	sts := &appsv1.StatefulSet{}
	err := k8sClient.Get(ctx, client.ObjectKey{Name: name, Namespace: namespace}, sts)
	if err == nil {
		return true
	}
	if !apierrors.IsNotFound(err) {
		log.Info("Unable to query StatefulSet", "name", name, "namespace", namespace, "error", err)
	}
	return false
}

// Teardown undeploys DACS components and cleans up the cluster.
func Teardown(ctx context.Context, k8sClient client.Client, namespace string, supportBundleDir string) {

	if !skipSupportBundle {
		By("collecting support bundle")
		supportBundlePath := filepath.Join(supportBundleDir, "suite.tar.gz")
		Eventually(func(g Gomega, ctx context.Context) {
			output, err := utils.CollectSupportBundle(ctx, supportBundlePath, startTime)
			g.Expect(err).NotTo(HaveOccurred(), "Failed to collect support bundle: %s\n", output)
		}).WithContext(ctx).Should(Succeed(), "Failed to collect support bundle")
	}

	By("cleaning up test jobs")
	cmd := exec.CommandContext(ctx, "kubectl", "delete", "job", "--all", "-n", namespace, "--ignore-not-found=true", "--timeout=2m")
	output, err := cmd.CombinedOutput()
	if err != nil {
		log.Info("Job cleanup had issues", "error", err, "output", string(output))
	} else {
		log.Info("Test jobs cleaned up successfully", "output", string(output))
	}

	if !doUninstall {
		log.Info("Skipping uninstall (set UNINSTALL=true to uninstall)")
		return
	}

	By("uninstalling DACS")
	Eventually(func(g Gomega, ctx context.Context) {
		cmd := exec.CommandContext(ctx, "make", "helm-uninstall")
		_, err := utils.Run(cmd)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to uninstall DACS")
	}).WithTimeout(5*time.Minute).WithContext(ctx).Should(Succeed(), "Failed to uninstall DACS")

	if doUninstallPrereq {
		By("uninstalling cache server prerequisites")
		Eventually(func(g Gomega, ctx context.Context) {
			cmd := exec.CommandContext(ctx, "make", "helm-prereq-uninstall")
			_, err := utils.Run(cmd)
			g.Expect(err).NotTo(HaveOccurred(), "Failed to uninstall cache server prerequisites")
		}).WithTimeout(5*time.Minute).WithContext(ctx).Should(Succeed(), "Failed to uninstall cache server prerequisites")
	} else {
		log.Info("Skipping dacs-prereq uninstallation (set UNINSTALL_PREREQ=true to uninstall)")
	}

	if namespace != "kube-system" && namespace != "dacs" {
		By("removing namespace")
		Eventually(func(g Gomega, ctx context.Context) {
			ns := &corev1.Namespace{
				ObjectMeta: metav1.ObjectMeta{
					Name: namespace,
				},
			}
			err := k8sClient.Delete(ctx, ns)
			if !apierrors.IsNotFound(err) {
				g.Expect(err).NotTo(HaveOccurred(), "Failed to delete namespace")
			}
		}).WithContext(ctx).Should(Succeed(), "Namespace deletion failed")
	}
}

// PostReport generates the junit report for the test and copies it to
// the junit report path.
func PostReport(ctx SpecContext, r Report, junitReport, summaryReport string) {
	suiteConfig, _ := GinkgoConfiguration()
	if suiteConfig.DryRun {
		log.Info("Skipping report generation as this is a dry run")
		return
	}
	By("generating junit report")
	Expect(junitReport).NotTo(BeEmpty(), "junit report path is empty")
	reportPath, err := getArtifactPath(junitReport)
	Expect(err).NotTo(HaveOccurred(), "failed to get absolute path for junit report")

	config := reporters.JunitReportConfig{
		OmitTimelinesForSpecState: types.SpecStateSkipped | types.SpecStatePending,
		OmitLeafNodeType:          true,
		OmitSuiteSetupNodes:       true,
	}

	By("generating path to absolute path: " + reportPath)
	Expect(reporters.GenerateJUnitReportWithConfig(r, reportPath, config)).
		To(Succeed(), "fail to generate junit report")
	Expect(utils.FixupJunitReport(reportPath)).
		To(Succeed(), "failed to fixup junit report for junit")

	if summaryReport != "" {
		Expect(summaryReport).NotTo(BeEmpty(), "summary report path is empty")
		summaryPath, err := getArtifactPath(summaryReport)
		Expect(err).NotTo(HaveOccurred(), "failed to get absolute path for summary report")
		By("Generating markdown report")
		cmd := exec.CommandContext(ctx, "kubectl", "config", "current-context")
		output, err := utils.Run(cmd)
		Expect(err).NotTo(HaveOccurred(), "Failed to get current context")
		clusterName := strings.TrimSpace(output)
		Expect(clusterName).NotTo(BeEmpty(), "Failed to get cluster name")
		// Generate a simple summary report
		generateSummaryReport(r, clusterName, summaryPath)
	}
}

func getArtifactPath(path string) (string, error) {
	reportPath := path
	if filepath.IsAbs(path) {
		return reportPath, nil
	}
	projectDir, err := utils.GetProjectDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(projectDir, path), nil
}

// generateSummaryReport generates a simple markdown summary report.
func generateSummaryReport(r Report, clusterName, outputPath string) error {
	var passed, failed, skipped int
	for _, spec := range r.SpecReports {
		switch spec.State {
		case types.SpecStatePassed:
			passed++
		case types.SpecStateFailed:
			failed++
		case types.SpecStateSkipped, types.SpecStatePending:
			skipped++
		}
	}

	content := fmt.Sprintf(`# DACS E2E Test Summary

**Cluster:** %s
**Run Duration:** %s

## Results

| Status | Count |
|--------|-------|
| ✅ Passed | %d |
| ❌ Failed | %d |
| ⏭️ Skipped | %d |
| **Total** | %d |

`, clusterName, r.RunTime.String(), passed, failed, skipped, passed+failed+skipped)

	if failed > 0 {
		content += "## Failed Tests\n\n"
		for _, spec := range r.SpecReports {
			if spec.State == types.SpecStateFailed {
				content += fmt.Sprintf("- %s\n", spec.FullText())
			}
		}
	}

	return os.WriteFile(outputPath, []byte(content), 0644)
}

// CollectSuiteSupportBundle collects the support bundle for the suite if the
// support bundle collection is not skipped.
func CollectSuiteSupportBundle(ctx context.Context, supportBundleDir string) {
	suiteConfig, _ := GinkgoConfiguration()
	if suiteConfig.DryRun {
		log.Info("Skipping report generation as this is a dry run")
		return
	}
	if !skipSupportBundle {
		By("Collecting support bundle for the suite")
		supportBundlePath := filepath.Join(supportBundleDir, "suite.tar.gz")

		log.Info("Collecting support bundle for suite", "path", supportBundlePath)
		Eventually(func(g Gomega, ctx context.Context) {
			output, err := utils.CollectSupportBundle(ctx, supportBundlePath, startTime)
			g.Expect(err).NotTo(HaveOccurred(), "Failed to collect support bundle: %s", output)
		}).WithContext(ctx).Should(Succeed(), "Failed to collect support bundle")
	}
}

// ensureCacheReady validates cache-server components and skips scheduler checks when controller-managed.
func ensureCacheReady(ctx context.Context, k8sClient client.Client, namespace, statefulSetName string, controllerManaged bool) {
	VerifyStatefulSetReady(ctx, k8sClient, namespace, statefulSetName)

	if controllerManaged {
		log.Info("Skipping cacheserver-scheduler readiness check", "reason", "controller-managed deployment")
		return
	}

	VerifyPodUp(ctx, k8sClient, namespace, "app=cacheserver-scheduler")
}

func ensureControllerInstall(ctx context.Context, k8sClient client.Client) {
	By("validating dacs controller deployment is ready")
	Eventually(func(g Gomega) {
		deploy := &appsv1.Deployment{}
		err := k8sClient.Get(ctx, client.ObjectKey{
			Name:      controllerDeploymentName,
			Namespace: controllerNamespace,
		}, deploy)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to get controller deployment")
		g.Expect(deploy.Spec.Replicas).NotTo(BeNil(), "Controller deployment has no replica spec")
		g.Expect(deploy.Status.ReadyReplicas).To(Equal(*deploy.Spec.Replicas),
			fmt.Sprintf("Controller deployment %s not ready", deploy.Name))
	}).WithContext(ctx).Should(Succeed())

	By("validating Cache CRD is installed")
	cmd := exec.CommandContext(ctx, "kubectl", "get", "crd", cacheControllerCRDName)
	_, err := utils.Run(cmd)
	Expect(err).NotTo(HaveOccurred(), "Cache CRD %s is not installed", cacheControllerCRDName)
}

// VerifyStatefulSetReady validates that a StatefulSet has all expected replicas ready.
func VerifyStatefulSetReady(ctx context.Context, k8sClient client.Client, namespace, name string) {
	By(fmt.Sprintf("validating that StatefulSet %s has all replicas ready", name))
	Eventually(func(g Gomega) {
		sts := &appsv1.StatefulSet{}
		err := k8sClient.Get(ctx, client.ObjectKey{Name: name, Namespace: namespace}, sts)
		g.Expect(err).NotTo(HaveOccurred(), fmt.Sprintf("Failed to get StatefulSet %s", name))

		g.Expect(sts.Spec.Replicas).NotTo(BeNil(),
			fmt.Sprintf("StatefulSet %s has nil spec.replicas", name))
		g.Expect(sts.Status.ReadyReplicas).To(Equal(*sts.Spec.Replicas),
			fmt.Sprintf("StatefulSet %s has %d/%d ready replicas", name, sts.Status.ReadyReplicas, *sts.Spec.Replicas))

		log.Info("StatefulSet is ready", "name", name, "ready", sts.Status.ReadyReplicas, "desired", *sts.Spec.Replicas)
	}).WithContext(ctx).Should(Succeed())
}

// VerifyPodUp validates that pods with the given label are running.
func VerifyPodUp(ctx context.Context, k8sClient client.Client, namespace, podLabel string) {
	By(fmt.Sprintf("validating that the pod with label %s is running as expected", podLabel))
	verifyPodUp := func(g Gomega) {
		// Parse the label selector
		selector, err := metav1.ParseToLabelSelector(podLabel)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to parse label selector")

		labelSelector, err := metav1.LabelSelectorAsSelector(selector)
		g.Expect(err).NotTo(HaveOccurred(), "Failed to convert label selector")

		// List pods with the given label
		podList := &corev1.PodList{}
		err = k8sClient.List(ctx, podList, &client.ListOptions{
			Namespace:     namespace,
			LabelSelector: labelSelector,
		})
		g.Expect(err).NotTo(HaveOccurred(), fmt.Sprintf("Failed to retrieve pod information for label %s", podLabel))

		// Filter out pods that are being deleted
		var activePods []corev1.Pod
		for _, pod := range podList.Items {
			if pod.DeletionTimestamp == nil {
				activePods = append(activePods, pod)
			}
		}
		g.Expect(activePods).ToNot(BeEmpty(), fmt.Sprintf("expected at least 1 pod with label %s running", podLabel))

		// Validate each pod's status
		for _, pod := range activePods {
			log.Info("Checking pod status", "pod", pod.Name)
			g.Expect(pod.Status.Phase).To(Equal(corev1.PodRunning), fmt.Sprintf("Incorrect pod status for pod %s", pod.Name))
		}
	}
	Eventually(verifyPodUp).WithContext(ctx).Should(Succeed())
}

// GetCacheName returns the cache name for the current deployment.
// When the new controller is present, the cache name is ControllerCacheName;
// otherwise it falls back to LegacyStatefulSetName.
func GetCacheName(ctx context.Context, k8sClient client.Client) string {
	if controllerDeploymentExists(ctx, k8sClient) {
		return ControllerCacheName
	}
	return LegacyStatefulSetName
}

// GetDiscoveryURL returns the FQDN of the cache server discovery service
// using the legacy discovery service name.
func GetDiscoveryURL(namespace string) string {
	return GetDiscoveryURLForCache(LegacyStatefulSetName, namespace)
}

// GetDiscoveryURLForCache returns the FQDN of the discovery service for a
// given cache name. The discovery service is named "<cacheName>-discovery".
func GetDiscoveryURLForCache(cacheName, namespace string) string {
	return fmt.Sprintf("%s-discovery.%s.svc.cluster.local", cacheName, namespace)
}

// GetNodeList returns the list of nodes in the cluster.
func GetNodeList(ctx context.Context, k8sClient client.Client) (*corev1.NodeList, error) {
	var nodeList corev1.NodeList
	err := k8sClient.List(ctx, &nodeList)
	if err != nil {
		return nil, err
	}
	return &nodeList, nil
}

// GetClientID retrieves the managed identity client ID from node labels.
func GetClientID(ctx context.Context, k8sClient client.Client) (string, error) {
	nodeList, err := GetNodeList(ctx, k8sClient)
	if err != nil {
		return "", fmt.Errorf("Failed to get node list from cluster: %v", err)
	}
	if len(nodeList.Items) == 0 {
		return "", fmt.Errorf("No nodes found in the cluster")
	}

	for _, node := range nodeList.Items {
		if val, exists := node.Labels[KUBERNETES_NODE_CLIENT_ID_KEY]; exists {
			return val, nil
		}
	}
	return "", fmt.Errorf("No managed identity clientId found on any node")
}

// GetStorageAccessEnv determines the storage access method and returns the appropriate
// environment variable key and value (either SAS token or managed identity client ID).
func GetStorageAccessEnv(ctx context.Context, k8sClient client.Client, storageAccountName string) (string, string, error) {
	value, isSASKeyExist := os.LookupEnv(CACHECLIENT_SAS_TOKEN_KEY)
	if isSASKeyExist {
		return CACHECLIENT_SAS_TOKEN_KEY, value, nil
	}
	clientID, err := GetClientID(ctx, k8sClient)
	if err != nil {
		return "", "", fmt.Errorf("No storage access environment variable found for account: %s error: %v", storageAccountName, err)
	}
	return CACHECLIENT_CLIENT_ID_KEY, clientID, nil
}

// CreateSASTokenSecret creates a Kubernetes Secret containing the SAS token
// and applies it to the cluster.
func CreateSASTokenSecret(ctx context.Context, k8sClient client.Client, name, namespace, sasToken string) (*corev1.Secret, error) {
	s := secret.NewSecret().
		WithName(name).
		WithNamespace(namespace).
		WithData(map[string]string{
			CACHECLIENT_SAS_TOKEN_KEY: sasToken,
		})
	if err := k8sClient.Create(ctx, s.Secret); err != nil {
		return nil, fmt.Errorf("failed to create SAS token secret %s/%s: %w", namespace, name, err)
	}
	return s.Secret, nil
}

// WorkloadIdentitySAYAML creates a ServiceAccount YAML configured for Azure
// workload identity. It reads the perf-client SA fixture, sets the name,
// namespace, and workload identity client ID annotation.
func WorkloadIdentitySAYAML(name, namespace, clientID string) ([]byte, error) {
	if clientID == "" || name == "" {
		return nil, fmt.Errorf("workload identity client ID and service account name must be set")
	}

	data, err := fixtures.ReadPerfClientServiceAccount()
	if err != nil {
		return nil, fmt.Errorf("failed to read service account fixture: %w", err)
	}

	var sa corev1.ServiceAccount
	if err := yaml.Unmarshal(data, &sa); err != nil {
		return nil, fmt.Errorf("failed to unmarshal service account YAML: %w", err)
	}

	sa.Name = name
	sa.Namespace = namespace
	if sa.Annotations == nil {
		sa.Annotations = make(map[string]string)
	}
	sa.Annotations["azure.workload.identity/client-id"] = clientID

	return yaml.Marshal(&sa)
}
