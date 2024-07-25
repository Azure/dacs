{{/* Generate image. If image contains registry or prefix */}}
{{- define "amlArcExtension.getImage" }}
  {{- $privateRegistry := "" }}
  {{- if .Values.Azure.containerRegistry.repository }}
    {{- $privateRegistry = .Values.Azure.containerRegistry.repository }}
  {{- end -}}

  {{- $registry := .Values.global.registry }}
  {{- $prefix := .Values.global.prefix }}
  {{- $tag := .Values.global.version }}

  {{- $parts := splitList "/" .image }}
  {{- if contains "." (first $parts) }}
    {{- $registry = (first $parts) }}
    {{- if gt (len $parts) 2 }}
      {{- $prefix = (initial (rest $parts)) | join "/" }}
    {{- end }}
  {{- else }}
    {{- if gt (len $parts) 1 }}
      {{ $prefix = (initial $parts) | join "/" }}
    {{- end }}
  {{- end }}
  {{- if gt (len $privateRegistry) 0 }}
    {{- $registry = $privateRegistry }}
  {{- end }}
  {{- $nameAndTag := (last $parts) }}
  {{- $nameAndTagParts := split ":" $nameAndTag }}
  {{- $name := $nameAndTagParts._0 }}
  {{- if gt (len $nameAndTagParts) 1 }}
    {{- $tag = $nameAndTagParts._1 }}
  {{- end }}
  
  {{- if gt (len $prefix) 0 }}
    {{- printf "%s/%s/%s:%s" $registry $prefix $name $tag }}
  {{- else }}
    {{- printf "%s/%s:%s" $registry $name $tag }}
  {{- end }}
{{- end }}

{{/* Get image pull secrets section */}}
{{- define "amlArcExtension.imagePullSecrets" }}
{{- if .Values.Azure.containerRegistry.repository }}
imagePullSecrets:
- name: {{ .Values.imagePullSecretName }}
- name: {{ .Values.imagePullSecretName }}-hook
{{- end }}
{{- end }}

{{/* Get image pull secrets content */}}
{{- define "amlArcExtension.imagePullSecretContent" }}
{{- $registry := .Values.Azure.containerRegistry.repository }}
{{- $username := .Values.Azure.containerRegistry.username }}
{{- $password := .Values.Azure.containerRegistry.password }}
{{- printf "{\"auths\":{\"%s\":{\"username\":\"%s\",\"password\":\"%s\",\"auth\":\"%s\"}}}" $registry $username $password (printf "%s:%s" $username $password | b64enc) | b64enc }}
{{- end }}