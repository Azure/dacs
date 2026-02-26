/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Constants for DACS e2e tests
# --------------------------------------------------------------------------------------------------------------------
*/
package common

const (
	DEFAULT_CACHESERVER_SERVICE_NAME = "cacheserver"
	DEFAULT_CACHESERVER_NAME         = "cacheserver"
	DEFAULT_CACHESERVER_NAMESPACE    = "cache-server"
	DEFAULT_CACHECLIENT_IMAGE        = "dacs-client:local"
	DEFAULT_DISCOVERY_SERVICE_NAME   = "cacheserver-discovery"
	CACHECLIENT_SAS_TOKEN_KEY        = "STORAGE_INTCPT_SAS_TOKEN"
	CACHECLIENT_CLIENT_ID_KEY        = "AZURE_CLIENT_ID"

	// AKS nodes will hold below key to get the managed identity clientId
	KUBERNETES_NODE_CLIENT_ID_KEY = "kubernetes.azure.com/kubelet-identity-client-id"
)
