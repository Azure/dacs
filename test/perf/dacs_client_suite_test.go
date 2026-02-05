/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# End to End test for multiple cache clients performing writes
# --------------------------------------------------------------------------------------------------------------------
*/
package perf

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/utils"
	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"k8s.io/client-go/tools/clientcmd"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/client/config"
)

var testMultipleDACSClients = func(ctx context.Context) {
	var count int
	var storageAccount string
	flag.Parse()
	err := validateWriteMode(fluxOpts.WriteMode)
	Expect(err).ShouldNot(HaveOccurred(), "Invalid write mode provided")

	storageAccount = fluxOpts.StorageAccount
	if storageAccount == "" {
		// Fallback to read from environment variable
		storageAccount = os.Getenv("STORAGE_ACCOUNT")
	}

	if storageAccount == "" {
		Fail("Storage account name must be provided via -storageAccount flag (OR) setting STORAGE_ACCOUNT environment variable")
	}
	// Build K8s client
	k8sClient := getK8sClientOrDie()
	now := time.Now().Format(time.RFC3339)
	now = strings.ReplaceAll(strings.ToLower(now), ":", "-")

	// Get no.of clients to be deployed based on k8s nodes
	if fluxOpts.ClientCount > 0 {
		log.Info("Using user provided cache client count: %d", fluxOpts.ClientCount)
		count = int(fluxOpts.ClientCount)
	} else {
		nodeList, err := getNodeList(k8sClient)
		Expect(err).ShouldNot(HaveOccurred(), "Failed to get node list from cluster")
		count = len(nodeList.Items)
	}
	config := &CacheClientConfig{
		k8sClient:            k8sClient,
		count:                count,
		cacheServerNamespace: common.DEFAULT_CACHESERVER_NAMESPACE,
		cacheClientImage:     getDefaultImage(),
		storageAccountName:   storageAccount,
		containerName:        containerNamePrefix + now,
		blobCount:            fluxOpts.BlobCount,
		blobSize:             fluxOpts.BlobSize,
		writeMode:            fluxOpts.WriteMode,
	}

	config.dacsClientTestSuite(ctx)
}

var _ = PDescribe("Multiple DACS Clients E2E Tests", Label("dacsclients", "perf", "aks"), Ordered, func() {
	Context("Multiple DACS Clients Test Suite", Label("dacsclients"), func() {
		BeforeAll(func(ctx context.Context) {
			Eventually(func(g Gomega) {
				// Create namespace for cacheclients
				cmd := exec.CommandContext(ctx, "kubectl", "get", "ns", cacheClientNs)
				if _, err := utils.Run(cmd); err != nil {
					cmd := exec.CommandContext(ctx, "kubectl", "create", "ns", cacheClientNs)
					_, err := utils.Run(cmd)
					g.Expect(err).NotTo(HaveOccurred(), "Failed to create namespace")
				}
			}).WithContext(ctx).Should(Succeed(), "Namespace creation failed or namespace not found")

			DeferCleanup(func(ctx context.Context) {
				Eventually(func(g Gomega, ctx context.Context) {
					cmd := exec.CommandContext(ctx, "kubectl", "delete", "ns", cacheClientNs, "--ignore-not-found", "--wait")
					_, err := utils.Run(cmd)
					g.Expect(err).NotTo(HaveOccurred(), "Failed to delete namespace")
				}).WithContext(ctx).Should(Succeed(), "Namespace deletion failed")
			})
		})

		It("Should run multiple dacs cache clients performing writes", testMultipleDACSClients)
	})
})

// getDefaultImage returns the default dacs client image based on environment variables
func getDefaultImage() string {
	if fluxOpts.ClientImage != "" {
		return fluxOpts.ClientImage
	}
	return common.GetDefaultImage()
}

func validateWriteMode(mode string) error {
	if _, exists := utils.WriteSupportModes[mode]; !exists {
		return fmt.Errorf("unsupported write mode: %s", mode)
	}
	return nil
}

func getK8sClientOrDie() client.Client {
	if os.Getenv(clientcmd.RecommendedConfigPathEnvVar) == "" {
		err := os.Setenv(clientcmd.RecommendedConfigPathEnvVar, defaultKubeConfigPath)
		if err != nil {
			log.Error(err, "Failed to set default kubeconfig path")
			os.Exit(1)
		}
	}
	// Set up controller-runtime client
	cfg, err := config.GetConfig()
	if err != nil {
		log.Error(err, "Failed to get kubeconfig")
		os.Exit(1)
	}
	k8sClient, err := client.New(cfg, client.Options{})
	if err != nil {
		log.Error(err, "Failed to create controller-runtime client")
		os.Exit(1)
	}
	return k8sClient
}
