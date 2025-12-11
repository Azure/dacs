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
