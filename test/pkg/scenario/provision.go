/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
# Data provisioning - ensures model data exists in blob storage, mirrored from Hugging Face
# --------------------------------------------------------------------------------------------------------------------
*/
package scenario

import (
	"context"
	"fmt"
	"strings"
	"time"

	"sigs.k8s.io/controller-runtime/pkg/client"

	"github.com/Azure/dacs/test/pkg/kube/job"
)

// ProvisionModelData ensures model data is available in blob storage.
// It checks if data exists, and if not, downloads from HF and uploads to blob.
func ProvisionModelData(ctx context.Context, k8sClient client.Client, config ModelLoadingConfig, scale ScaleProfile) error {
	log.Info("Provisioning model data", "scale", scale.Name, "hfRepo", scale.HFRepo, "blobPrefix", scale.BlobPrefix())

	jobName := fmt.Sprintf("scenario-provision-%s",
		strings.ToLower(strings.ReplaceAll(strings.ReplaceAll(scale.Name, "-", ""), "/", "")))
	if len(jobName) > 63 {
		jobName = jobName[:63]
	}

	provisionScript, err := workloadScripts.ReadFile("workloads/provision.py")
	if err != nil {
		return fmt.Errorf("failed to read provision.py: %w", err)
	}

	logs, err := job.RunPython(ctx, k8sClient, job.PythonConfig{
		Name:      jobName,
		Namespace: config.Namespace,
		Image:     config.TestImage,
		Script:    string(provisionScript),
		Env: map[string]string{
			"BLOB_PREFIX": scale.BlobPrefix(),
			"HF_REPO":     scale.HFRepo,
		},
		Timeout:                  60 * time.Minute,
		StorageAccount:           config.StorageAccount,
		StorageContainer:         config.StorageContainer,
		WorkloadIdentityClientID: config.WorkloadIdentityClientID,
		WorkloadIdentitySA:       config.WorkloadIdentitySA,
	})
	if logs != "" {
		log.Info("Provision job output", "logs", logs)
	}
	if err != nil {
		return err
	}

	log.Info("Model data provisioned successfully", "scale", scale.Name)
	return nil
}
