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
	"errors"
	"fmt"
	"time"

	. "github.com/onsi/ginkgo/v2"
	"github.com/onsi/ginkgo/v2/types"
	. "github.com/onsi/gomega"

	akshelpers "github.com/Azure/dacs/test/pkg/aks"
	"github.com/Azure/dacs/test/pkg/kusto"
	"github.com/Azure/dacs/test/pkg/utils"
)

const (
	restartLogFlushDelay = time.Minute
	// AKS requires services to finish shutting down before a start operation.
	// https://learn.microsoft.com/en-us/azure/aks/start-stop-cluster
	restartShutdownSettleDelay = 30 * time.Minute
	restartOperationTimeout    = 30 * time.Minute
	restartStateTimeout        = 30 * time.Minute
	restartRecoveryTimeout     = 90 * time.Minute
)

func testClusterRestart(tc *testContext) {
	It("should serve data after an AKS cluster restart", Label("aks-restart"), func(ctx SpecContext) {
		By("allowing logs to flush before stopping the cluster")
		Expect(utils.Sleep(ctx, restartLogFlushDelay)).To(Succeed(), "waiting for logs to flush before restart")

		var stoppedAt time.Time
		started := false
		DeferCleanup(func(ctx SpecContext) {
			if started {
				return
			}
			By("attempting to recover the cluster after an interrupted restart")
			state := waitForRestartOperation(ctx, tc)
			if state.PowerState == "Running" && state.ProvisioningState == "Succeeded" {
				return
			}
			if stoppedAt.IsZero() {
				stoppedAt = time.Now()
			}
			Expect(utils.Sleep(ctx, time.Until(stoppedAt.Add(restartShutdownSettleDelay)))).
				To(Succeed(), "allowing shutdown to settle before recovery")
			err := startRestartCluster(ctx, tc)
			if errors.Is(err, akshelpers.ErrCapacity) && CurrentSpecReport().State == types.SpecStateSkipped {
				log.Error(err, "cluster recovery could not allocate capacity; resource cleanup or manual recovery is required")
				AddReportEntry("AKS restart recovery failure", err.Error())
				return
			}
			Expect(err).NotTo(HaveOccurred(), "recovering AKS cluster after interrupted restart")
			state = waitForRestartOperation(ctx, tc)
			Expect(state).To(Equal(akshelpers.ClusterState{PowerState: "Running", ProvisioningState: "Succeeded"}),
				"AKS cluster did not recover after interrupted restart")
		}, NodeTimeout(restartRecoveryTimeout))

		By("stopping the AKS cluster")
		stopCtx, cancelStop := context.WithTimeout(ctx, restartOperationTimeout)
		stopStart := time.Now()
		err := akshelpers.StopCluster(stopCtx, tc.subscription, tc.resourceGroupName, tc.managedCluster, log)
		stopDuration := time.Since(stopStart)
		cancelStop()
		if errors.Is(err, akshelpers.ErrCapacity) {
			Skip(fmt.Sprintf("Azure capacity constraint while stopping cluster: %v", err))
		}
		Expect(err).NotTo(HaveOccurred(), "stopping AKS cluster")
		state := waitForRestartOperation(ctx, tc)
		Expect(state).To(Equal(akshelpers.ClusterState{PowerState: "Stopped", ProvisioningState: "Succeeded"}),
			"AKS cluster did not reach the stopped state")
		stoppedAt = time.Now()

		By("allowing AKS shutdown to settle before starting the cluster")
		Expect(utils.Sleep(ctx, restartShutdownSettleDelay)).To(Succeed(), "waiting for AKS shutdown to settle")

		By("starting the AKS cluster")
		startStart := time.Now()
		err = startRestartCluster(ctx, tc)
		startDuration := time.Since(startStart)
		if errors.Is(err, akshelpers.ErrCapacity) {
			Skip(fmt.Sprintf("Azure could not allocate capacity to restart cluster: %v", err))
		}
		Expect(err).NotTo(HaveOccurred(), "starting AKS cluster")
		state = waitForRestartOperation(ctx, tc)
		Expect(state).To(Equal(akshelpers.ClusterState{PowerState: "Running", ProvisioningState: "Succeeded"}),
			"AKS cluster did not reach the running state")
		started = true

		// Metric metadata uses Helm, so emit only after the API is back online.
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: "aks-stop"}, stopDuration)
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: "aks-start"}, startDuration)
	})
}

func startRestartCluster(ctx context.Context, tc *testContext) error {
	startCtx, cancel := context.WithTimeout(ctx, restartOperationTimeout)
	defer cancel()
	return akshelpers.StartCluster(startCtx, tc.subscription, tc.resourceGroupName, tc.managedCluster, log)
}

func waitForRestartOperation(ctx context.Context, tc *testContext) akshelpers.ClusterState {
	GinkgoHelper()
	var state akshelpers.ClusterState
	Eventually(func(g Gomega, ctx context.Context) {
		var err error
		state, err = akshelpers.GetClusterState(ctx, tc.subscription, tc.resourceGroupName, tc.managedCluster)
		g.Expect(err).NotTo(HaveOccurred(), "querying AKS cluster state")
		log.Info("AKS cluster state", "cluster", tc.managedCluster, "state", state)
		g.Expect(state.ProvisioningState).To(BeElementOf("Succeeded", "Failed", "Canceled"),
			"waiting for AKS management-plane operation to finish")
	}).WithContext(ctx).WithTimeout(restartStateTimeout).WithPolling(30*time.Second).
		Should(Succeed(), "AKS management-plane operation did not finish")
	return state
}
