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

package e2e

import (
	"os"
	"strings"
	"time"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/utils/ptr"
	"sigs.k8s.io/controller-runtime/pkg/client"
)

var _ = Describe("Private image registry", Label("e2e", "controller", "registry"), func() {
	It("uses localized images and pull secrets in actual pods created for a new Cache", func(ctx SpecContext) {
		registry := strings.TrimSuffix(os.Getenv("EXPECTED_IMAGE_REGISTRY"), "/")
		if registry == "" {
			Skip("Set EXPECTED_IMAGE_REGISTRY for private-registry qualification")
		}
		var managers appsv1.DeploymentList
		Expect(k8sClient.List(ctx, &managers, client.InNamespace(ControllerNamespace),
			client.MatchingLabels{"control-plane": "controller-manager"})).To(Succeed())
		Expect(managers.Items).To(HaveLen(1))
		managerPod := managers.Items[0].Spec.Template.Spec
		var image, secretNames string
		for _, container := range managerPod.Containers {
			Expect(container.Image).To(HavePrefix(registry + "/"))
			for _, env := range container.Env {
				switch env.Name {
				case "CACHE_CONTROLLER_CACHESERVERIMAGE":
					image = env.Value
				case "CACHE_CONTROLLER_IMAGEPULLSECRETS":
					secretNames = env.Value
				}
			}
		}
		Expect(image).To(HavePrefix(registry + "/"))
		var secrets []corev1.LocalObjectReference
		if secretNames != "" {
			for _, name := range strings.Split(secretNames, ",") {
				secrets = append(secrets, corev1.LocalObjectReference{Name: name})
			}
		}
		Expect(managerPod.ImagePullSecrets).To(Equal(secrets))
		cache := &storagev1.Cache{
			ObjectMeta: metav1.ObjectMeta{GenerateName: "registry-check-", Namespace: namespace},
			Spec: storagev1.CacheSpec{
				Scaling:        storagev1.ScalingType{Static: &storagev1.StaticScaleConfig{NumServers: 1}},
				ServerSizeInGB: 1,
				Storage:        &storagev1.StorageConfig{Ephemeral: ptr.To(true)},
			},
		}
		Expect(k8sClient.Create(ctx, cache)).To(Succeed())
		DeferCleanup(func(ctx SpecContext) {
			Expect(client.IgnoreNotFound(k8sClient.Delete(ctx, cache))).To(Succeed())
		})
		Eventually(func(g Gomega) {
			var pods corev1.PodList
			g.Expect(k8sClient.List(ctx, &pods, client.InNamespace(namespace),
				client.MatchingLabels{CacheNameLabelKey: cache.Name})).To(Succeed())
			g.Expect(pods.Items).To(HaveLen(1))
			pod := pods.Items[0]
			g.Expect(pod.Spec.Containers).To(HaveLen(1))
			g.Expect(pod.Spec.Containers[0].Image).To(Equal(image))
			g.Expect(pod.Spec.ImagePullSecrets).To(Equal(secrets))
			g.Expect(pod.Status.ContainerStatuses).To(HaveLen(1))
			g.Expect(pod.Status.ContainerStatuses[0].ImageID).NotTo(BeEmpty())
			g.Expect(pod.Status.ContainerStatuses[0].Ready).To(BeTrue())
		}).WithContext(ctx).WithTimeout(5 * time.Minute).Should(Succeed())
	})
})
