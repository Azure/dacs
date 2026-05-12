package verification

import (
	"context"
	"fmt"
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
	cacheServerLabelKey   = "app"
	cacheServerLabelValue = "cacheserver"
	controllerLabelKey    = "control-plane"
	controllerLabelValue  = "controller-manager"
)

// VerifyInfraHealth checks that all cluster nodes have the Ready condition set
// to True. This should be called inside an Eventually block with a 10-minute
// timeout.
func VerifyInfraHealth(g Gomega, ctx context.Context, k8sClient client.Client, log logr.Logger) {
	verifyNodesReady(g, ctx, k8sClient, log)
}

// VerifyAppHealth checks application-level health: the cache controller deployment is
// ready, the Cache CR has a Ready condition, the cacheserver StatefulSet has all
// replicas ready, and all pods in the cache namespace are running.
func VerifyAppHealth(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
	verifyCacheCR(g, ctx, k8sClient, namespace, log)
	verifyControllerDeployment(g, ctx, k8sClient, namespace, log)
	verifyStatefulSet(g, ctx, k8sClient, namespace, log)
	verifyCachePods(g, ctx, k8sClient, namespace, log)
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
			"ready", deploy.Status.ReadyReplicas, "desired", desired)
		g.Expect(deploy.Spec.Replicas).NotTo(BeNil(),
			"controller deployment %s/%s spec.replicas is nil", deploy.Namespace, deploy.Name)
		g.Expect(deploy.Status.ReadyReplicas).To(Equal(*deploy.Spec.Replicas),
			"controller deployment %s/%s not ready: %d/%d replicas",
			deploy.Namespace, deploy.Name, deploy.Status.ReadyReplicas, *deploy.Spec.Replicas)
	}
}

func verifyStatefulSet(g Gomega, ctx context.Context, k8sClient client.Client, namespace string, log logr.Logger) {
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
		log.Info("StatefulSet", "name", sts.Name,
			"ready", sts.Status.ReadyReplicas, "desired", desired)
		g.Expect(sts.Spec.Replicas).NotTo(BeNil(),
			"statefulset %s/%s spec.replicas is nil", sts.Namespace, sts.Name)
		g.Expect(sts.Status.ReadyReplicas).To(Equal(*sts.Spec.Replicas),
			"statefulset %s/%s not ready: %d/%d replicas",
			sts.Namespace, sts.Name, sts.Status.ReadyReplicas, *sts.Spec.Replicas)
	}
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
