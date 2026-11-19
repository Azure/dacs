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
	"net"
	"net/url"
	"os"
	"os/exec"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"sigs.k8s.io/controller-runtime/pkg/client/config"

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

		By("waiting for the AKS cluster to finish starting")
		cmd = exec.CommandContext(ctx, "az", "aks", "wait",
			"--subscription", tc.subscription,
			"--name", tc.managedCluster,
			"--resource-group", tc.resourceGroupName,
			"--custom", "provisioningState=='Succeeded' && powerState.code=='Running'",
			"--interval", "30",
			"--timeout", "600",
		)
		_, err = utils.Run(cmd)
		Expect(err).NotTo(HaveOccurred(), "waiting for AKS cluster provisioning to succeed and power state to be Running")

		By("logging API server DNS before and after flushing the local DNS cache")
		cfg, err := config.GetConfig()
		Expect(err).NotTo(HaveOccurred(), "reading kubeconfig for API server DNS diagnostics")
		apiURL, err := url.Parse(cfg.Host)
		Expect(err).NotTo(HaveOccurred(), "parsing API server URL for DNS diagnostics")
		if resolvConf, err := os.ReadFile("/etc/resolv.conf"); err != nil {
			log.Error(err, "failed to read resolver configuration")
		} else {
			log.Info("resolver configuration after AKS start", "resolvConf", string(resolvConf))
		}
		logAPIAddresses := func(phase string) {
			lookupCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
			defer cancel()
			addresses, err := net.DefaultResolver.LookupHost(lookupCtx, apiURL.Hostname())
			if err != nil {
				log.Error(err, "API server DNS lookup failed", "phase", phase, "host", apiURL.Hostname())
				return
			}
			log.Info("API server DNS resolution", "phase", phase, "host", apiURL.Hostname(), "addresses", addresses)
		}
		logAPIAddresses("before local DNS cache flush")
		if resolvectl, err := exec.LookPath("resolvectl"); err != nil {
			log.Info("skipping local DNS cache flush", "reason", err.Error())
		} else {
			flushCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
			cmd = exec.CommandContext(flushCtx, resolvectl, "flush-caches")
			if os.Geteuid() != 0 {
				cmd = exec.CommandContext(flushCtx, "sudo", "-n", resolvectl, "flush-caches")
			}
			output, err := utils.Run(cmd)
			cancel()
			if err != nil {
				log.Error(err, "local DNS cache flush failed; continuing with recovery checks", "output", output)
			} else {
				log.Info("local DNS cache flushed")
			}
		}
		logAPIAddresses("after local DNS cache flush")

		// Metric metadata uses Helm, so emit only after the cluster has started.
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opAKSStop}, stopDuration)
		emitOperationMetric(ctx, kusto.ResilienceOperationResult{Operation: opAKSStart}, startDuration)
	})
}
