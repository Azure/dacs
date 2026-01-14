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

	"github.com/Azure/dacs/test/pkg/logger"
	"github.com/Azure/dacs/test/pkg/utils"
)

const (
	// trueString is the string representation of a boolean true value.
	trueString = "true"
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
		By("validating that cache server pods are running")
		VerifyCacheServerUp(ctx, k8sClient, namespace)
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

	By("validating that cache server pods are running")
	VerifyCacheServerUp(ctx, k8sClient, namespace)
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

// VerifyCacheServerUp validates that the cache server pods are running as expected.
func VerifyCacheServerUp(ctx context.Context, k8sClient client.Client, namespace string) {
	// Verify scheduler pod is running
	VerifyPodUp(ctx, k8sClient, namespace, "app=cacheserver-scheduler")

	// Verify cache server statefulset pods are ready
	VerifyStatefulSetReady(ctx, k8sClient, namespace, "cacheserver")
}

// VerifyStatefulSetReady validates that a StatefulSet has all expected replicas ready.
func VerifyStatefulSetReady(ctx context.Context, k8sClient client.Client, namespace, name string) {
	By(fmt.Sprintf("validating that StatefulSet %s has all replicas ready", name))
	Eventually(func(g Gomega) {
		sts := &appsv1.StatefulSet{}
		err := k8sClient.Get(ctx, client.ObjectKey{Name: name, Namespace: namespace}, sts)
		g.Expect(err).NotTo(HaveOccurred(), fmt.Sprintf("Failed to get StatefulSet %s", name))

		// Check that the StatefulSet has the expected number of replicas
		g.Expect(sts.Status.Replicas).To(BeNumerically(">", 0), fmt.Sprintf("StatefulSet %s has no replicas", name))

		// Check that all replicas are ready
		g.Expect(sts.Status.ReadyReplicas).To(Equal(sts.Status.Replicas),
			fmt.Sprintf("StatefulSet %s has %d/%d ready replicas", name, sts.Status.ReadyReplicas, sts.Status.Replicas))

		log.Info("StatefulSet is ready", "name", name, "ready", sts.Status.ReadyReplicas, "total", sts.Status.Replicas)
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
