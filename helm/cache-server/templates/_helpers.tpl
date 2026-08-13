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

{{/* Get the cache server address list */}}
{{- define "amlArcExtension.getCacheServerList" }}
{{- if .Values.cacheServer.enabled }}
{{- $cacheNamespace  := .Release.Namespace -}}
{{- range $cacheIdx, $e := until (.Values.cacheServer.numServers | int) -}}
{{- if $cacheIdx -}}#{{- end -}}{{- printf "cacheserver-%d.cacheserver.%s.svc.cluster.local" $cacheIdx $cacheNamespace }}
{{- end }}
{{- end }}
{{- end }}

{{/* Get the cache server discovery endpoint */}}
{{- define "amlArcExtension.getCacheServerDiscoveryEndpoint" }}
{{- if .Values.cacheServer.enabled }}
{{- $cacheNamespace  := .Release.Namespace -}}
{{- printf "cacheserver-discovery.%s.svc.cluster.local" $cacheNamespace }}
{{- end }}
{{- end }}
