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
Expand the name of the chart.
*/}}
{{- define "dacs-prereq.name" -}}
{{- .Values.name }}
{{- end }}

{{/*
Create the rollout checker name.
*/}}
{{- define "dacs-prereq.rolloutCheckName" -}}
{{- printf "%s-rollout" .Values.name }}
{{- end }}

{{/*
Common labels
*/}}
{{- define "dacs-prereq.labels" -}}
helm.sh/chart: {{ include "dacs-prereq.chart" . }}
{{ include "dacs-prereq.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | replace "+" "_" | trunc 63 | trimSuffix "-" | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
app.kubernetes.io/component: nvme-setup
app.kubernetes.io/part-of: dacs
{{- end }}

{{/*
Selector labels
*/}}
{{- define "dacs-prereq.selectorLabels" -}}
app.kubernetes.io/name: {{ include "dacs-prereq.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "dacs-prereq.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Replace source hosts only when explicitly opted in; retain repository subpaths.
*/}}
{{- define "dacs-prereq.image" -}}
{{- $repository := .image.repository -}}
{{- if and (eq (.global.registryMode | default "legacy") "replace") .global.registry -}}
{{- $repository = regexReplaceAll "^([^/]*[.:][^/]*|localhost)/" $repository "" -}}
{{- $repository = printf "%s/%s" (.global.registry | trimSuffix "/") $repository -}}
{{- end -}}
{{- printf "%s:%s" $repository (.image.tag | default .appVersion) -}}
{{- end }}
