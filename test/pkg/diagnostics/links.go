package diagnostics

import (
	"context"
	"fmt"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/utils"
)

// WorkspaceInfo identifies a Log Analytics workspace for deep-link generation.
type WorkspaceInfo struct {
	SubscriptionID string
	ResourceGroup  string
	Name           string
}

// LogLinks holds clickable diagnostic links for common queries.
type LogLinks struct {
	CacheServerLogs string
	ControllerLogs  string
	KubeEvents      string
	ASI             string
}

// GetWorkspaceFromCluster discovers the Log Analytics workspace linked to an
// AKS cluster via the OMS agent addon. Returns an error if the cluster has
// no monitoring addon configured.
func GetWorkspaceFromCluster(ctx context.Context, subscription, resourceGroup, clusterName string) (WorkspaceInfo, error) {
	cmd := exec.CommandContext(ctx,
		"az", "aks", "show",
		"--subscription", subscription,
		"--name", clusterName,
		"--resource-group", resourceGroup,
		"--query", "addonProfiles.omsagent.config.logAnalyticsWorkspaceResourceID",
		"-o", "tsv",
	)
	output, err := utils.Run(cmd)
	if err != nil {
		return WorkspaceInfo{}, fmt.Errorf("failed to query OMS agent config: %w", err)
	}
	resourceID := strings.TrimSpace(output)
	if resourceID == "" {
		return WorkspaceInfo{}, fmt.Errorf("no Log Analytics workspace configured on cluster %s", clusterName)
	}
	return parseWorkspaceResourceID(resourceID)
}

// parseWorkspaceResourceID extracts subscription, resource group, and workspace
// name from an ARM resource ID of the form:
//
//	/subscriptions/<sub>/resourceGroups/<rg>/providers/Microsoft.OperationalInsights/workspaces/<name>
func parseWorkspaceResourceID(resourceID string) (WorkspaceInfo, error) {
	parts := strings.Split(resourceID, "/")
	var ws WorkspaceInfo
	for i, part := range parts {
		if strings.EqualFold(part, "subscriptions") && i+1 < len(parts) {
			ws.SubscriptionID = parts[i+1]
		}
		if strings.EqualFold(part, "resourceGroups") && i+1 < len(parts) {
			ws.ResourceGroup = parts[i+1]
		}
		if strings.EqualFold(part, "workspaces") && i+1 < len(parts) {
			ws.Name = parts[i+1]
		}
	}
	if ws.SubscriptionID == "" || ws.ResourceGroup == "" || ws.Name == "" {
		return WorkspaceInfo{}, fmt.Errorf("failed to parse workspace resource ID: %s", resourceID)
	}
	return ws, nil
}

// GenerateLogLinks builds diagnostic deep-links (ADX queries and ASI search)
// scoped to the given time window, namespace, and cluster. The KQL queries
// are filtered by `_ResourceId` to the AKS cluster's ARM resource ID so logs
// from other clusters sharing the workspace are excluded.
func GenerateLogLinks(ws WorkspaceInfo, start, end time.Time, namespace, subscription, resourceGroup, clusterName string) LogLinks {
	clusterURI := fmt.Sprintf(
		"https://ade.loganalytics.io/subscriptions/%s/resourceGroups/%s/providers/Microsoft.OperationalInsights/workspaces/%s",
		ws.SubscriptionID, ws.ResourceGroup, ws.Name,
	)
	clusterResourceID := fmt.Sprintf(
		"/subscriptions/%s/resourceGroups/%s/providers/Microsoft.ContainerService/managedClusters/%s",
		subscription, resourceGroup, clusterName,
	)
	return LogLinks{
		CacheServerLogs: adxDeepLink(clusterURI, ws.Name, cacheServerKQL(start, end, namespace, clusterResourceID)),
		ControllerLogs:  adxDeepLink(clusterURI, ws.Name, controllerKQL(start, end, namespace, clusterResourceID)),
		KubeEvents:      adxDeepLink(clusterURI, ws.Name, kubeEventsKQL(start, end, namespace, clusterResourceID)),
		ASI:             asiLink(start, end, clusterName),
	}
}

func cacheServerKQL(start, end time.Time, namespace, clusterResourceID string) string {
	return fmt.Sprintf(
		`ContainerLogV2
| where _ResourceId =~ "%s"
| where TimeGenerated between (datetime(%s) .. datetime(%s))
| where PodNamespace == "%s"
| where PodName startswith "cacheserver"
| project TimeGenerated, PodName, LogMessage, LogSource
| order by TimeGenerated asc`,
		clusterResourceID, kqlTime(start), kqlTime(end), namespace,
	)
}

func controllerKQL(start, end time.Time, namespace, clusterResourceID string) string {
	return fmt.Sprintf(
		`ContainerLogV2
| where _ResourceId =~ "%s"
| where TimeGenerated between (datetime(%s) .. datetime(%s))
| where PodNamespace == "%s"
| where PodName startswith "dacs-manager"
| project TimeGenerated, PodName, LogMessage, LogSource
| order by TimeGenerated asc`,
		clusterResourceID, kqlTime(start), kqlTime(end), namespace,
	)
}

func kubeEventsKQL(start, end time.Time, namespace, clusterResourceID string) string {
	_ = namespace
	return fmt.Sprintf(
		`KubeEvents
| where _ResourceId =~ "%s"
| where TimeGenerated between (datetime(%s) .. datetime(%s))
| project TimeGenerated, ObjectKind, Name, Namespace, Reason, Message
| order by TimeGenerated asc`,
		clusterResourceID, kqlTime(start), kqlTime(end),
	)
}

// kqlTime formats a time.Time as a KQL datetime literal.
func kqlTime(t time.Time) string {
	return t.UTC().Format("2006-01-02T15:04:05Z")
}

// adxDeepLink builds a URL that opens the given KQL query in Azure Data Explorer.
func adxDeepLink(clusterURI, database, kql string) string {
	return fmt.Sprintf(
		"https://dataexplorer.azure.com/clusters/%s/databases/%s?query=%s",
		url.PathEscape(clusterURI),
		url.PathEscape(database),
		url.QueryEscape(kql),
	)
}

// asiLink builds a URL that opens Azure Service Insights scoped to the given
// cluster and time range.
func asiLink(start, end time.Time, clusterName string) string {
	return fmt.Sprintf(
		"https://asi.azure.ms/search/services/AKS?searchText=%s&globalFrom=%s&globalTo=%s&resources=%s",
		url.QueryEscape(clusterName),
		url.QueryEscape(start.UTC().Format(time.RFC3339)),
		url.QueryEscape(end.UTC().Format(time.RFC3339)),
		url.QueryEscape(`["Managed Clusters"]`),
	)
}

// WriteLinksSummary writes a markdown file with clickable ADX deep-links.
// The file is intended for consumption by Azure DevOps pipeline summary
// (##vso[task.uploadsummary]) so links appear in the pipeline Extensions tab.
func WriteLinksSummary(links LogLinks, outputPath string) error {
	if err := os.MkdirAll(filepath.Dir(outputPath), 0o755); err != nil {
		return fmt.Errorf("failed to create directory for links summary: %w", err)
	}

	content := "## Diagnostic Links\n\n" +
		"### Azure Service Insights\n\n" +
		"- [ASI - AKS Cluster Overview](" + links.ASI + ")\n\n" +
		"### Azure Data Explorer Queries\n\n" +
		"- [Cache Server Logs (ContainerLogV2)](" + links.CacheServerLogs + ")\n" +
		"- [Controller Logs (ContainerLogV2)](" + links.ControllerLogs + ")\n" +
		"- [Kube Events](" + links.KubeEvents + ")\n"

	if err := os.WriteFile(outputPath, []byte(content), 0o644); err != nil {
		return fmt.Errorf("failed to write links summary: %w", err)
	}
	return nil
}
