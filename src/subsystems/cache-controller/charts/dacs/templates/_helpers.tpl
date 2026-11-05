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

{{/*
Chart name based on project name.
Truncated to 63 characters for Kubernetes compatibility.
*/}}
{{- define "chart.name" -}}
{{- .Values.namePrefix | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Full name for resources. Uses namePrefix directly for simplicity.
Truncated to 63 characters for Kubernetes compatibility.
*/}}
{{- define "chart.fullname" -}}
{{- .Values.namePrefix | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Namespace for generated references.
Always uses the Helm release namespace.
*/}}
{{- define "chart.namespaceName" -}}
{{ .Release.Namespace }}
{{- end }}



{{/*
Service name with proper truncation for Kubernetes 63-character limit.
Takes a context with .suffix for the service type (e.g., "webhook-service").
If fullname + suffix exceeds 63 chars, truncates fullname to 45 chars.
*/}}
{{- define "chart.serviceName" -}}
{{- $fullname := include "chart.fullname" .context -}}
{{- if gt (len $fullname) 45 -}}
{{- printf "%s-%s" (trunc 45 $fullname | trimSuffix "-") .suffix | trunc 63 | trimSuffix "-" -}}
{{- else -}}
{{- printf "%s-%s" $fullname .suffix | trunc 63 | trimSuffix "-" -}}
{{- end -}}
{{- end }}

{{/*
Common labels for Helm charts.
Includes app version, chart version, app name, instance, and managed-by labels.
*/}}
{{- define "chart.labels" -}}
{{- if .Chart.AppVersion -}}
app.kubernetes.io/version: {{ .Chart.AppVersion | replace "+" "_" | trunc 63 | trimSuffix "-" | quote }}
{{- end }}
{{- if .Chart.Version }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" | quote }}
{{- end }}
app.kubernetes.io/name: {{ include "chart.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels for matching pods and services.
Only includes name and instance for consistent selection.
*/}}
{{- define "chart.selectorLabels" -}}
app.kubernetes.io/name: {{ include "chart.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
ServiceAccount name for the controller manager.
Uses fullname + "-controller-manager" suffix for umbrella chart compatibility.
*/}}
{{- define "chart.serviceAccountName" -}}
{{- include "chart.fullname" . }}-controller-manager
{{- end }}

{{/*
Name of the PriorityClass created and used by the controller manager.
*/}}
{{- define "chart.priorityClassName" -}}
{{- include "chart.fullname" . }}-priority
{{- end }}

{{/*
A nonempty image.registry always replaces the source host.
Otherwise, registryMode controls global.registry: replace for every image,
or legacy prefixing only for the controller and generated CacheServer images.
*/}}
{{- define "chart.image" -}}
{{- $global := .global | default dict -}}
{{- $mode := $global.registryMode | default "legacy" -}}
{{- $registry := $global.registry | default "" -}}
{{- $repository := .image.repository -}}
{{- if or .image.registry (eq $mode "replace") -}}
  {{- $registry = .image.registry | default $registry | trimSuffix "/" -}}
  {{- if $registry -}}
  {{- $repository = regexReplaceAll "^([^/]*[.:][^/]*|localhost)/" $repository "" -}}
  {{- end -}}
{{- else if not .legacyRegistry -}}
  {{- $registry = "" -}}
{{- end -}}
{{- if $registry -}}
{{- $repository = printf "%s/%s" $registry $repository -}}
{{- end -}}
{{- $tag := .image.tag -}}
{{- if .appVersion -}}
{{- $tag = $tag | default .appVersion -}}
{{- end -}}
{{- $repository -}}:{{- $tag -}}
{{- end -}}
