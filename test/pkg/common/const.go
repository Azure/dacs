/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
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
