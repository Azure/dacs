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
	"os/exec"
	"slices"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/utils"
	"github.com/Azure/dacs/test/pkg/verification"
)

const opAKSNodeCrash = "aks-node-crash"

func testClusterNodeCrash(tc *testContext) {
	It("should survive ungraceful crashes of each cacheserver node", Label("aks-node-crash"), func(ctx SpecContext) {
		if len(tc.initialNodeNames) < 2 {
			Skip("AKS node crash test requires at least two nodes")
		}

		By("selecting nodes hosting cacheserver pods")
		var pods corev1.PodList
		Expect(tc.List(ctx, &pods, client.InNamespace(tc.namespace), client.MatchingLabels{"app": "cacheserver"})).
			To(Succeed(), "listing cacheserver pods")
		Expect(pods.Items).NotTo(BeEmpty(), "no cacheserver pods found")
		nodeNames := make([]string, 0, len(pods.Items))
		for _, pod := range pods.Items {
			Expect(pod.Spec.NodeName).NotTo(BeEmpty(), "cacheserver pod %s is not scheduled", pod.Name)
			nodeNames = append(nodeNames, pod.Spec.NodeName)
		}
		slices.Sort(nodeNames)
		nodeNames = slices.Compact(nodeNames)

		for i, victimNode := range nodeNames {
			By(fmt.Sprintf("crashing cacheserver node %d/%d: %s", i+1, len(nodeNames), victimNode))
			crashClusterNode(ctx, tc, victimNode)

			By("waiting for cluster health after the node crash")
			Eventually(func(g Gomega, ctx context.Context) {
				verification.VerifyInfraHealth(g, ctx, tc.Client, log)
				verification.VerifyAppHealth(g, ctx, tc.Client, tc.namespace, log)
			}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).
				Should(Succeed(), "cluster did not recover after crashing node %s", victimNode)

			By("verifying cache serving after the node crash")
			Expect(verification.VerifyCacheServing(ctx, tc.Client, tc.namespace, log)).
				To(Succeed(), "cache serving verification failed after crashing node %s", victimNode)
		}
	})
}

func crashClusterNode(ctx SpecContext, tc *testContext, victimNode string) {
	GinkgoHelper()

	By("recording cacheserver state before the node crash")
	var pods corev1.PodList
	Expect(tc.List(ctx, &pods, client.InNamespace(tc.namespace), client.MatchingLabels{"app": "cacheserver"})).
		To(Succeed(), "listing cacheserver pods")
	Expect(pods.Items).NotTo(BeEmpty(), "no cacheserver pods found")
	var victimPod *corev1.Pod
	for i := range pods.Items {
		if pods.Items[i].Spec.NodeName == victimNode {
			victimPod = &pods.Items[i]
			break
		}
	}
	Expect(victimPod).NotTo(BeNil(), "node %s no longer hosts a cacheserver pod", victimNode)
	restartCount, found := containerRestartCount(*victimPod, "cacheserver")
	Expect(found).To(BeTrue(), "victim pod %s has no cacheserver container status", victimPod.Name)
	image := cacheserverImage(*victimPod)
	Expect(image).NotTo(BeEmpty(), "victim pod %s has no cacheserver image", victimPod.Name)

	log.Info("selected victim", "node", victimNode, "pod", victimPod.Name)

	By("creating a privileged crash helper on the victim node")
	var crashPod = corev1.Pod{
		ObjectMeta: metav1.ObjectMeta{
			GenerateName: "node-crash-helper-",
			Namespace:    tc.namespace,
		},
		Spec: corev1.PodSpec{
			NodeName:      victimNode,
			RestartPolicy: corev1.RestartPolicyAlways,
			Containers: []corev1.Container{{
				Name:    "node-crash",
				Image:   image,
				Command: []string{"sh", "-c", "while true; do sleep 3600; done"},
				SecurityContext: &corev1.SecurityContext{
					Privileged: ptr.To(true),
				},
			}},
		},
	}
	Expect(tc.Create(ctx, &crashPod)).To(Succeed(), "creating crash helper on node %s", victimNode)
	DeferCleanup(func(ctx SpecContext) {
		Expect(client.IgnoreNotFound(tc.Delete(ctx, &crashPod))).
			To(Succeed(), "deleting crash helper %s/%s", crashPod.Namespace, crashPod.Name)
	})
	Eventually(func(g Gomega, ctx context.Context) {
		g.Expect(tc.Get(ctx, client.ObjectKeyFromObject(&crashPod), &crashPod)).
			To(Succeed(), "getting crash helper %s", crashPod.Name)
		g.Expect(crashPod.Status.Phase).To(Equal(corev1.PodRunning), "crash helper should be running")
		g.Expect(allContainersReady(crashPod)).To(BeTrue(), "crash helper containers should be ready")
	}).WithContext(ctx).WithTimeout(2*time.Minute).WithPolling(2*time.Second).
		Should(Succeed(), "crash helper did not become ready")

	By("crashing the victim node")
	var node corev1.Node
	Expect(tc.Get(ctx, client.ObjectKey{Name: victimNode}, &node)).
		To(Succeed(), "getting victim node %s before crash", victimNode)
	initialBootID := node.Status.NodeInfo.BootID
	Expect(initialBootID).NotTo(BeEmpty(), "victim node %s has no boot ID", victimNode)

	crashStarted := time.Now()
	crashCtx, cancelCrash := context.WithCancel(ctx)
	crashDone := make(chan error, 1)
	crashCmd := exec.CommandContext(crashCtx, "kubectl", "exec", crashPod.Name,
		"-n", tc.namespace, "--", "sh", "-c", "echo c > /proc/sysrq-trigger")
	// Exec can hang or disconnect when the host panics; verify impact below.
	go func() {
		_, err := utils.Run(crashCmd)
		crashDone <- err
	}()
	DeferCleanup(func() {
		cancelCrash()
		if err := <-crashDone; err != nil {
			log.Info("crash exec ended; disconnects during the panic are expected", "error", err)
		}
	})

	By("waiting for the victim node to reboot and recover automatically")
	Eventually(func(g Gomega, ctx context.Context) {
		var node corev1.Node
		g.Expect(tc.Get(ctx, client.ObjectKey{Name: victimNode}, &node)).
			To(Succeed(), "getting victim node %s", victimNode)
		g.Expect(node.Status.NodeInfo.BootID).NotTo(BeEmpty(), "victim node %s has no boot ID", victimNode)
		g.Expect(node.Status.NodeInfo.BootID).NotTo(Equal(initialBootID), "victim node %s has not rebooted", victimNode)
		g.Expect(node.Status.Conditions).To(ContainElement(SatisfyAll(
			HaveField("Type", corev1.NodeReady),
			HaveField("Status", corev1.ConditionTrue),
		)), "victim node %s should be ready", victimNode)
	}).WithContext(ctx).WithTimeout(15*time.Minute).WithPolling(15*time.Second).
		Should(Succeed(), "victim node %s did not reboot and recover", victimNode)
	cancelCrash()

	By("waiting for the victim's cacheserver workload to recover")
	Eventually(func(g Gomega, ctx context.Context) {
		var pods corev1.PodList
		g.Expect(tc.List(ctx, &pods, client.InNamespace(tc.namespace), client.MatchingLabels{"app": "cacheserver"})).
			To(Succeed(), "listing cacheserver pods")
		recovered := false
		for _, pod := range pods.Items {
			if pod.Spec.NodeName != victimNode || pod.Status.Phase != corev1.PodRunning || !allContainersReady(pod) {
				continue
			}
			currentRestarts, found := containerRestartCount(pod, "cacheserver")
			if pod.UID != victimPod.UID || (found && currentRestarts > restartCount) {
				recovered = true
				break
			}
		}
		g.Expect(recovered).To(BeTrue(), "no recreated or restarted ready cacheserver on node %s", victimNode)
	}).WithContext(ctx).WithTimeout(10*time.Minute).WithPolling(10*time.Second).
		Should(Succeed(), "cacheserver on victim node %s did not recover", victimNode)

	Expect(client.IgnoreNotFound(tc.Delete(ctx, &crashPod))).
		To(Succeed(), "deleting crash helper %s/%s after recovery", crashPod.Namespace, crashPod.Name)

	emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opAKSNodeCrash}, time.Since(crashStarted))
}

func allContainersReady(pod corev1.Pod) bool {
	return len(pod.Status.ContainerStatuses) > 0 && !slices.ContainsFunc(pod.Status.ContainerStatuses, func(status corev1.ContainerStatus) bool {
		return !status.Ready
	})
}

func containerRestartCount(pod corev1.Pod, name string) (int32, bool) {
	for _, status := range pod.Status.ContainerStatuses {
		if status.Name == name {
			return status.RestartCount, true
		}
	}
	return 0, false
}

// cacheserverImage prefers the running image digest to reuse the node's platform.
func cacheserverImage(pod corev1.Pod) string {
	for _, status := range pod.Status.ContainerStatuses {
		if status.Name == "cacheserver" && strings.Contains(status.ImageID, "@sha256:") {
			return strings.TrimPrefix(status.ImageID, "docker-pullable://")
		}
	}
	for _, container := range pod.Spec.Containers {
		if container.Name == "cacheserver" {
			return container.Image
		}
	}
	return ""
}
