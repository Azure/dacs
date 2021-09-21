<#
.Synopsis
  This script checks if docker container image specified by the ContainerImage argument exists locally.
  If image does not exist, the script will login to ACR and pull the image from ACR.
.Example
  Ensure-DockerImageAvailable.ps1 -ContainerImage dacs.azurecr.io/native-build:20210107T200158944
#>
param (
    [string]
    $ContainerImage
)

& docker inspect $ContainerImage *> $null

if ($LASTEXITCODE -ne 0) {
  $containerImageRepository = $ContainerImage.Substring(0, $ContainerImage.IndexOf('.'))
  Write-Host "Executing az acr login -n $containerImageRepository"
  az acr login -n $containerImageRepository

  $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
  Write-Host "Executing docker pull $ContainerImage"

  docker pull $ContainerImage

  Write-Host ("docker pull $ContainerImage took {0:0.##} seconds." -f $stopwatch.Elapsed.TotalSeconds)
}
