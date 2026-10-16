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
	"os/exec"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"

	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/utils"
)

const (
	restartLogFlushDelay = time.Minute
	opAKSStop            = "aks-stop"
	opAKSStart           = "aks-start"
)

func testClusterRestart(tc *testContext) {
	It("should serve data after an AKS cluster restart", Label("aks-restart"), func(ctx SpecContext) {
		By("allowing logs to flush before stopping the cluster")
		Expect(utils.Sleep(ctx, restartLogFlushDelay)).To(Succeed(), "waiting for logs to flush before restart")

		By("stopping the AKS cluster")
		cmd := exec.CommandContext(ctx, "az", "aks", "stop",
			"--subscription", tc.subscription,
			"--name", tc.managedCluster,
			"--resource-group", tc.resourceGroupName,
		)
		stopStart := time.Now()
		_, err := utils.Run(cmd)
		stopDuration := time.Since(stopStart)
		Expect(err).NotTo(HaveOccurred(), "stopping AKS cluster")

		By("starting the AKS cluster")
		cmd = exec.CommandContext(ctx, "az", "aks", "start",
			"--subscription", tc.subscription,
			"--name", tc.managedCluster,
			"--resource-group", tc.resourceGroupName,
		)
		startStart := time.Now()
		_, err = utils.Run(cmd)
		startDuration := time.Since(startStart)
		Expect(err).NotTo(HaveOccurred(), "starting AKS cluster")

		// Metric metadata uses Helm, so emit only after the cluster has started.
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opAKSStop}, stopDuration)
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opAKSStart}, startDuration)
	})
}
