param(
    [string]$SubscriptionId,
    [string]$ResourceGroup,
    [string]$ClusterName,
    [string]$Namespace = "default",
    [string]$ReleaseName = "cache-server",
    [string]$Version,
    [string]$ImageTag = "",
    [int]$CacheServerCount = 1
)

$ErrorActionPreference = "Stop"

# Set Azure subscription
az account set --subscription $SubscriptionId

# Get AKS credentials
az aks get-credentials --resource-group $ResourceGroup --name $ClusterName --overwrite-existing

# Create namespace if it doesn't exist
kubectl create namespace $Namespace --dry-run=client -o yaml | kubectl apply -f -

# Uninstall existing release and clean up resources
Write-Host "Cleaning up existing release..."
helm uninstall $ReleaseName -n $Namespace 2>$null
kubectl delete pods,svc,deployments,statefulsets,job -l app=cacheserver -n $Namespace --wait 2>$null

# Deploy with Helm
Write-Host "Starting Helm install..."
Write-Host "Version: $Version"
Write-Host "ImageTag: $ImageTag"
Write-Host "CacheServerCount: $CacheServerCount"

if ($CacheServerCount -lt 0) {
    throw "CacheServerCount must be >= 0. Provided: $CacheServerCount"
}

if ([string]::IsNullOrWhiteSpace($ImageTag)) {
    throw "ImageTag must be provided"
}

# Ensure Helm can pull the chart from ACR
Write-Host "Logging into ACR for Helm pull..."
$acrName = "dacs"
$token = az acr login --name $acrName --expose-token --output tsv --query accessToken
if ([string]::IsNullOrWhiteSpace($token)) {
    throw "Failed to obtain ACR token for $acrName"
}
echo $token | helm registry login "$acrName.azurecr.io" --username 00000000-0000-0000-0000-000000000000 --password-stdin
if ($LASTEXITCODE -ne 0) {
    throw "Helm registry login failed for $acrName.azurecr.io"
}

helm install $ReleaseName oci://dacs.azurecr.io/dacs/charts/cache-server `
    --namespace $Namespace `
    --version $Version `
    --values ./.pipelines/cacheserver/helm/values.yaml `
    --set cacheServer.numServers=$CacheServerCount `
    --set cacheServer.image.tag=$ImageTag `
    --set cacheServer.scheduler.image.tag=$ImageTag `
    --wait --timeout=5m

if ($LASTEXITCODE -ne 0) {
    Write-Host "Helm install failed. Collecting debugging information..." -ForegroundColor Red
    kubectl get pods -n $Namespace -o wide
    kubectl describe pods -n $Namespace
    kubectl get events -n $Namespace --sort-by='.lastTimestamp'
    kubectl logs -n $Namespace -l app=cacheserver-scheduler --tail=50
    exit 1
}

Write-Host ""
Write-Host "======================================" -ForegroundColor Green
Write-Host "✓ Cache Server Deployed!" -ForegroundColor Green
Write-Host "======================================" -ForegroundColor Green

# Show final status
kubectl get pods -n $Namespace -o wide
kubectl get svc -n $Namespace