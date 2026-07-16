package verification

import (
	"context"
	"fmt"
	"os/exec"
	"strings"

	"github.com/go-logr/logr"
	. "github.com/onsi/gomega"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	apimeta "k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
)

const (
	cacheServerContainerName = "cacheserver"
	cacheServerLabelKey      = "app"
	cacheServerLabelValue    = "cacheserver"
	discoveryConfigMountPath = "/serverConfig"
	controllerLabelKey       = "control-plane"
	controllerLabelValue     = "controller-manager"
)

// VerifyInfraHealth checks that all cluster nodes have the Ready condition set
// to True. This should be called inside an Eventually block with a 10-minute
// timeout.
func VerifyInfraHealth(g Gomega, ctx context.Context, k8sClient client.Client, log logr.Logger) {
	verifyNodesReady(g, ctx, k8sClient, log)
}

// VerifyAppHealth checks application-level health: the cache controller deployment is
// fully rolled out and ready, the Cache CR has a Ready condition, the cacheserver
// StatefulSet has completed any rolling update (all replicas updated to the current
// revision and ready), and all pods in the cache namespace are running. Use inside
// Eventually to wait for a rollout to settle.
func VerifyAppHealth(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
	VerifyAppHealthWithImage(g, ctx, k8sClient, namespace, "", log)
}

// VerifyAppHealthWithImage is VerifyAppHealth plus an assertion that the
// cacheserver StatefulSet's container image equals expectedImage. When
// expectedImage is empty the image check is skipped (behaves like
// VerifyAppHealth). Use this inside Eventually to wait for a helm upgrade
// or rollback to fully roll the StatefulSet to a specific image.
func VerifyAppHealthWithImage(g Gomega, ctx context.Context, k8sClient client.Client, namespace, expectedImage string, log logr.Logger) {
	verifyCacheCR(g, ctx, k8sClient, namespace, log)
	verifyControllerDeployment(g, ctx, k8sClient, namespace, log)
	verifyStatefulSet(g, ctx, k8sClient, namespace, expectedImage, log)
	verifyCachePods(g, ctx, k8sClient, namespace, log)
	verifyEndpoints(g, ctx, k8sClient, namespace, log)
}

func verifyCacheCR(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
	var cacheList storagev1.CacheList
	g.Expect(k8sClient.List(ctx, &cacheList, client.InNamespace(namespace))).To(Succeed(), "failed to list Cache CRs")
	g.Expect(cacheList.Items).NotTo(BeEmpty(), "no Cache CRs found in namespace %s", namespace)
	for _, c := range cacheList.Items {
		conds := summarizeConditions(c.Status.Conditions)
		log.Info("Cache CR", "name", c.Name, "conditions", conds)
		g.Expect(apimeta.IsStatusConditionTrue(c.Status.Conditions, storagev1.ReadyCondition)).To(BeTrue(),
			"Cache %s Ready condition is not True; conditions=%s", c.Name, conds)
	}
}

func verifyNodesReady(g Gomega, ctx context.Context, k8sClient client.Client, log logr.Logger) {
	var nodes corev1.NodeList
	g.Expect(k8sClient.List(ctx, &nodes)).To(Succeed(), "failed to list nodes")
	for _, n := range nodes.Items {
		ready := nodeReadyCondition(n.Status.Conditions)
		log.Info("Node", "name", n.Name, "ready", ready)
		g.Expect(ready).To(Equal(string(corev1.ConditionTrue)),
			"node %s not ready: ready=%s", n.Name, ready)
	}
}

func verifyControllerDeployment(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
	var deployments appsv1.DeploymentList
	g.Expect(k8sClient.List(ctx, &deployments,
		client.InNamespace(namespace),
		client.MatchingLabels{controllerLabelKey: controllerLabelValue},
	)).To(Succeed(), "failed to list controller deployments in namespace %s", namespace)

	if len(deployments.Items) == 0 {
		log.Info("no controller deployment found, skipping controller check")
		return
	}

	for _, deploy := range deployments.Items {
		desired := int32(0)
		if deploy.Spec.Replicas != nil {
			desired = *deploy.Spec.Replicas
		}
		log.Info("Controller deployment", "name", deploy.Name,
			"ready", deploy.Status.ReadyReplicas,
			"updated", deploy.Status.UpdatedReplicas,
			"available", deploy.Status.AvailableReplicas,
			"desired", desired,
			"observedGeneration", deploy.Status.ObservedGeneration,
			"generation", deploy.Generation)
		g.Expect(deploy.Spec.Replicas).NotTo(BeNil(),
			"controller deployment %s/%s spec.replicas is nil", deploy.Namespace, deploy.Name)
		g.Expect(deploy.Status.ObservedGeneration).To(BeNumerically(">=", deploy.Generation),
			"controller deployment %s/%s rollout not observed: observedGeneration=%d generation=%d",
			deploy.Namespace, deploy.Name, deploy.Status.ObservedGeneration, deploy.Generation)
		g.Expect(deploy.Status.UpdatedReplicas).To(Equal(*deploy.Spec.Replicas),
			"controller deployment %s/%s rollout in progress: %d/%d replicas updated",
			deploy.Namespace, deploy.Name, deploy.Status.UpdatedReplicas, *deploy.Spec.Replicas)
		g.Expect(deploy.Status.AvailableReplicas).To(Equal(*deploy.Spec.Replicas),
			"controller deployment %s/%s not available: %d/%d replicas available",
			deploy.Namespace, deploy.Name, deploy.Status.AvailableReplicas, *deploy.Spec.Replicas)
		g.Expect(deploy.Status.ReadyReplicas).To(Equal(*deploy.Spec.Replicas),
			"controller deployment %s/%s not ready: %d/%d replicas",
			deploy.Namespace, deploy.Name, deploy.Status.ReadyReplicas, *deploy.Spec.Replicas)
	}
}

func verifyStatefulSet(g Gomega, ctx context.Context, k8sClient client.Client, namespace, expectedImage string, log logr.Logger) {
	var stsList appsv1.StatefulSetList
	g.Expect(k8sClient.List(ctx, &stsList,
		client.InNamespace(namespace),
		client.MatchingLabels{cacheServerLabelKey: cacheServerLabelValue},
	)).To(Succeed(), "failed to list cacheserver StatefulSets in namespace %s", namespace)
	g.Expect(stsList.Items).NotTo(BeEmpty(),
		"no StatefulSet with label %s=%s found in namespace %s",
		cacheServerLabelKey, cacheServerLabelValue, namespace)

	for _, sts := range stsList.Items {
		desired := int32(0)
		if sts.Spec.Replicas != nil {
			desired = *sts.Spec.Replicas
		}

		image, err := getCacheServerImage(&sts)
		g.Expect(err).NotTo(HaveOccurred(), "getting cacheserver image from StatefulSet %s/%s", sts.Namespace, sts.Name)

		log.Info("StatefulSet", "name", sts.Name,
			"ready", sts.Status.ReadyReplicas,
			"updated", sts.Status.UpdatedReplicas,
			"current", sts.Status.CurrentReplicas,
			"desired", desired,
			"currentRevision", sts.Status.CurrentRevision,
			"updateRevision", sts.Status.UpdateRevision,
			"observedGeneration", sts.Status.ObservedGeneration,
			"generation", sts.Generation,
			"image", image,
			"expectedImage", expectedImage)
		g.Expect(sts.Spec.Replicas).NotTo(BeNil(),
			"statefulset %s/%s spec.replicas is nil", sts.Namespace, sts.Name)
		g.Expect(sts.Status.ObservedGeneration).To(BeNumerically(">=", sts.Generation),
			"statefulset %s/%s rollout not observed: observedGeneration=%d generation=%d",
			sts.Namespace, sts.Name, sts.Status.ObservedGeneration, sts.Generation)
		g.Expect(sts.Status.UpdateRevision).To(Equal(sts.Status.CurrentRevision),
			"statefulset %s/%s rollout in progress: currentRevision=%s updateRevision=%s",
			sts.Namespace, sts.Name, sts.Status.CurrentRevision, sts.Status.UpdateRevision)
		g.Expect(sts.Status.UpdatedReplicas).To(Equal(*sts.Spec.Replicas),
			"statefulset %s/%s rollout in progress: %d/%d replicas updated",
			sts.Namespace, sts.Name, sts.Status.UpdatedReplicas, *sts.Spec.Replicas)
		g.Expect(sts.Status.ReadyReplicas).To(Equal(*sts.Spec.Replicas),
			"statefulset %s/%s not ready: %d/%d replicas",
			sts.Namespace, sts.Name, sts.Status.ReadyReplicas, *sts.Spec.Replicas)
		if expectedImage != "" {
			g.Expect(image).To(Equal(expectedImage),
				"statefulset %s/%s image mismatch: got=%s want=%s",
				sts.Namespace, sts.Name, image, expectedImage)
		}
	}
}

// verifyEndpoints verifies that the endpoints for each cache server are correctly configured.
// There is a delay when the configmap is updated and the pods need to pick up the changes for
// some of the data validations
func verifyEndpoints(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
	var stsList appsv1.StatefulSetList
	g.Expect(k8sClient.List(ctx, &stsList,
		client.InNamespace(namespace),
		client.MatchingLabels{cacheServerLabelKey: cacheServerLabelValue},
	)).To(Succeed(), "failed to list cacheserver StatefulSets in namespace %s", namespace)

	for _, sts := range stsList.Items {
		g.Expect(sts.Spec.Replicas).NotTo(BeNil(), "statefulset %s/%s spec.replicas is nil", sts.Namespace, sts.Name)

		// Resolve the discovery ConfigMap name from the StatefulSet's volume
		// spec. This handles both controller-managed deployments (where the
		// name is <cache-name>-discovery-config) and legacy Helm deployments
		// (where it is hardcoded as cacheserver-discovery-config).
		cmName := resolveDiscoveryConfigMapName(&sts)
		g.Expect(cmName).NotTo(BeEmpty(),
			"statefulset %s/%s: could not find discovery ConfigMap volume (looked for volume mount at %s)",
			sts.Namespace, sts.Name, discoveryConfigMountPath)
		var cm corev1.ConfigMap
		g.Expect(k8sClient.Get(ctx, client.ObjectKey{
			Namespace: sts.Namespace,
			Name:      cmName,
		}, &cm)).To(Succeed(), "failed to get discovery ConfigMap %s/%s", sts.Namespace, cmName)

		log.Info("Verifying discovery ConfigMap", "statefulset", sts.Name, "configmap", cmName, "data", cm.Data)
		for i := 0; i < int(*sts.Spec.Replicas); i++ {
			podName := fmt.Sprintf("%s-%d", sts.Name, i)
			for key, expectedValue := range cm.Data {
				filePath := fmt.Sprintf("%s/%s", discoveryConfigMountPath, key)
				cmd := exec.CommandContext(ctx, "kubectl", "exec", "-n", sts.Namespace, podName, "--", "cat", filePath)
				output, err := cmd.CombinedOutput()
				g.Expect(err).To(Succeed(), "failed to exec cat %s on pod %s: %s", filePath, podName, string(output))
				gotValue := strings.TrimRight(string(output), "\n")
				wantValue := strings.TrimRight(expectedValue, "\n")
				g.Expect(gotValue).To(Equal(wantValue),
					"ConfigMap %s key %s content mismatch in pod %s: got %q, want %q",
					cmName, key, podName, gotValue, wantValue)
			}
		}
	}
	log.Info("All cache server pods have correct discovery ConfigMap content")
}

// resolveDiscoveryConfigMapName inspects the StatefulSet's pod template to find
// the ConfigMap backing the discovery config volume mount. It finds the volume
// mount at discoveryConfigMountPath, then looks up the corresponding volume to
// extract the ConfigMap name. Returns empty string if not found.
func resolveDiscoveryConfigMapName(sts *appsv1.StatefulSet) string {
	// Find which volume name is mounted at the discovery config path.
	var volumeName string
	for _, c := range sts.Spec.Template.Spec.Containers {
		for _, vm := range c.VolumeMounts {
			if vm.MountPath == discoveryConfigMountPath {
				volumeName = vm.Name
				break
			}
		}
		if volumeName != "" {
			break
		}
	}
	if volumeName == "" {
		return ""
	}

	// Look up the volume to get the ConfigMap name.
	for _, v := range sts.Spec.Template.Spec.Volumes {
		if v.Name == volumeName && v.ConfigMap != nil {
			return v.ConfigMap.Name
		}
	}
	return ""
}

func getCacheServerImage(sts *appsv1.StatefulSet) (string, error) {
	if sts == nil {
		return "", fmt.Errorf("statefulset is nil")
	}
	containers := sts.Spec.Template.Spec.Containers
	for _, c := range containers {
		if c.Name == cacheServerContainerName {
			return c.Image, nil
		}
	}
	return "", fmt.Errorf("no container named '%s' found in statefulset %s/%s", cacheServerContainerName, sts.Namespace, sts.Name)
}

func verifyCachePods(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
	var pods corev1.PodList
	g.Expect(k8sClient.List(ctx, &pods, client.InNamespace(namespace))).To(Succeed(), "failed to list cache pods")
	g.Expect(pods.Items).NotTo(BeEmpty(), "no pods in cache namespace")
	for _, p := range pods.Items {
		ready := containersReady(p.Status.ContainerStatuses)
		log.Info("Pod", "name", p.Name, "phase", p.Status.Phase, "containersReady", ready)
		if p.Status.Phase == corev1.PodSucceeded {
			continue
		}
		g.Expect(p.Status.Phase).To(Equal(corev1.PodRunning),
			"pod %s not running: phase=%s", p.Name, p.Status.Phase)
		for _, cs := range p.Status.ContainerStatuses {
			g.Expect(cs.Ready).To(BeTrue(),
				"container %s in pod %s not ready", cs.Name, p.Name)
		}
	}
}

func summarizeConditions(conds []metav1.Condition) string {
	if len(conds) == 0 {
		return "[]"
	}
	parts := make([]string, 0, len(conds))
	for _, c := range conds {
		parts = append(parts, fmt.Sprintf("%s=%s", c.Type, c.Status))
	}
	return "[" + strings.Join(parts, ",") + "]"
}

func nodeReadyCondition(conds []corev1.NodeCondition) string {
	for _, c := range conds {
		if c.Type == corev1.NodeReady {
			return string(c.Status)
		}
	}
	return "Missing"
}

func containersReady(cs []corev1.ContainerStatus) string {
	ready := 0
	for _, c := range cs {
		if c.Ready {
			ready++
		}
	}
	return fmt.Sprintf("%d/%d", ready, len(cs))
}
