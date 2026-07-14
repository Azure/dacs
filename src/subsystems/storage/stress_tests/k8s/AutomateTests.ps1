# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.

# Differences between this script and the bash version
# We do not start servers since we assume that they are already running
# can add this in later if necessary

param(
  [Parameter(Mandatory = $true)]
  [string]
  $KubeConfig,

  [Parameter(Mandatory = $true)]
  [string]
  $OutputDir,

  [Parameter(Mandatory = $false)]
  [string]
  $TestConfigFile
)

function Write-LogOutput {
    [CmdletBinding()]
    Param(
        [Parameter(Position=0, Mandatory=$true, ValueFromPipeline=$true)]
        [string[]]$Message,

        [Parameter(Position=1, Mandatory=$false)]
        [ValidateScript({Test-Path -LiteralPath $_ -IsValid})]
        [string]$LogFile = '.\default.log',

        [Parameter(Mandatory=$false)]
        [switch]$Quiet,

        [Parameter(Mandatory=$false)]
        [switch]$PassThru
    )

    Process {
        $Message | ForEach-Object {
            $msg = "[{0:yyyy-MM-dd HH:mm:ss}]`t{1}" -f (Get-Date), $_
            if (-not $Quiet.IsPresent) {
                $msg | Out-Host
            }
            $msg
        } | Add-Content $LogFile
        if ($PassThru.IsPresent) {
            $Message
        }
    }
}

function Start-Pod {
    param (
        [Parameter(Mandatory=$true)]
        [string]$YamlFile
    )

    "Starting pod(s). Using yaml file at $YamlFile" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    kubectl --kubeconfig $KubeConfig apply -f $YamlFile
}

function Stop-Pod {
    param (
        [Parameter(Mandatory=$true)]
        [string]$YamlFile
    )

    "Stopping pod(s). Using yaml file at $YamlFile" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    kubectl --kubeconfig $KubeConfig delete -f $YamlFile
}

function Get-PodStatus {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Identifier
    )

    $Pattern = [regex] "$Identifier-[a-zA-Z0-9\-]*"
    $Statuses = [string](kubectl --kubeconfig $KubeConfig get pods | Where-Object {$_ -match $Pattern} | ForEach-Object {($_ -split "\s+")[2]})
    return $Statuses
}

function Wait-ForStatus {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Identifier,

        [Parameter(Mandatory=$true)]
        [string]$DesiredStatus,

        [Parameter(Mandatory=$true)]
        [int]$Timeout,

        [Parameter(Mandatory=$true)]
        [int]$WaitInterval
    )

    $t = 0
    while ($t -lt $Timeout) {
        $Statuses = Get-PodStatus -Identifier $Identifier
        $Statuses = $Statuses -split ' '
        $NotMatched = $false

        foreach ($Status in $Statuses) {
            if ($Status -ne $DesiredStatus) {
                $NotMatched = $true
                break
            }
        }

        if ($NotMatched) {
            "Waiting for Pod(s) $Identifier to enter status: $DesiredStatus. $t seconds have elapsed..." `
                | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            $t += $WaitInterval
            Start-Sleep -Seconds $WaitInterval
        }
        else {
            "Pod(s) $Identifier* have reached desired status: $DesiredStatus" `
                | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            return $true
        }
    }

    "Pod(s) $Identifier have failed to reach the desired status: $DesiredStatus" `
        | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    return $false
}

function Test-PodsRunning {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Identifier
    )

    $DesiredStatus = "Running"
    $Timeout = 60
    $WaitInterval = 2
    if (-not(Wait-ForStatus -Identifier $Identifier -DesiredStatus $DesiredStatus -Timeout $Timeout -WaitInterval $WaitInterval)) {
        "Pod(s) failed to start..." | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return $false
    }

    return $true
}

function Test-PodsCompleted {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Identifier
    )

    $DesiredStatus = "Completed"
    $Timeout = 1800
    $WaitInterval = 30
    if (-not(Wait-ForStatus -Identifier $Identifier -DesiredStatus $DesiredStatus -Timeout $Timeout -WaitInterval $WaitInterval)) {
        "Pod(s) failed to complete in specified time..." | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return $false
    }

    return $true
}


function Get-ClientConfigs {
    param (
        [Parameter(Mandatory=$true)]
        $TestArgs
    )

    $SetupDir = Join-Path $OutputDir "setup"
    if(-not(Test-Path $SetupDir)) {
        New-Item -ItemType Directory -Force -Path $SetupDir | Out-Null
    }

    $ClientTemplateFile = "stress-client.template.yaml"
    $ClientYamlFiles = @()

    for($counter = 0; $counter -lt $TestArgs.ClientConfigs.Count; $counter += 1) {
        $ClientName = $TestArgs.ClientConfigs[$counter].Name
        $ClientType = $TestArgs.ClientConfigs[$counter].ClientType
        $NumClients = $TestArgs.ClientConfigs[$counter].Parallelism
        $Dataset = $TestArgs.ClientConfigs[$counter].Dataset
        $Expiry = $TestArgs.ClientConfigs[$counter].KeyExpiry
        $ServerHostnames = $TestArgs.ClientConfigs[$counter].ServerHostnames
        $CacheGroupId = $TestArgs.ClientConfigs[$counter].GroupId

        $AccountName = $TestArgs.AccountName
        $AccountEndpoint = $TestArgs.AccountEndpoint
        $ContainerName = $TestArgs.ContainerName
        $SASToken = $TestArgs.SASToken

        $ClientYamlFile = Join-Path $SetupDir "stress-client-${ClientName}_${ClientType}_${NumClients}_${Dataset}_${Expiry}.yaml"
        Copy-Item -Path $ClientTemplateFile -Destination $ClientYamlFile -Force

        (Get-Content $ClientYamlFile) -replace '%CLIENTNAME%', $ClientName | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%CLIENT_TYPE%', $ClientType | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%PARALLELISM%', $NumClients | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%DATASET%', $Dataset | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%KEY_EXPIRY%', $Expiry | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%SERVER_HOSTNAMES%', $ServerHostnames | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%CACHE_GROUP_ID%', $CacheGroupId | Set-Content $ClientYamlFile

        (Get-Content $ClientYamlFile) -replace '%ACCOUNT_NAME%', $AccountName | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%ACCOUNT_ENDPOINT%', $AccountEndpoint | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%CONTAINER_NAME%', $ContainerName | Set-Content $ClientYamlFile
        (Get-Content $ClientYamlFile) -replace '%SAS_TOKEN%', $SASToken | Set-Content $ClientYamlFile

        $ClientYamlFiles += $ClientYamlFile
    }
    
    return $ClientYamlFiles
}

function Start-Client {
    param (
        [Parameter(Mandatory=$true)]
        [string]$YamlFile,

        [Parameter(Mandatory=$true)]
        [string]$ClientIdentifier
    )

    "Starting client(s) with identifier: $ClientIdentifier" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    Start-Pod -YamlFile $YamlFile

    if (-not(Test-PodsRunning -Identifier $ClientIdentifier)) {
        "Cache client(s) have failed to enter status Running..."| Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return $false
    }

    "Cache client(s) have entered status Running..." | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    return $true
}

function Stop-Client {
    param (
        [Parameter(Mandatory=$true)]
        [string]$YamlFile
    )

    Stop-Pod -YamlFile $YamlFile
}

function Get-ExpectedBytesToRead {
    param (
        [Parameter(Mandatory=$true)]
        [string]$Dataset,

        [Parameter(Mandatory=$true)]
        $TestArgs
    )

    $ctx = New-AzStorageContext -StorageAccountName $TestArgs.AccountName -SasToken $TestArgs.SASToken
    $BlobList = Get-AzStorageBlob -Container $TestArgs.ContainerName -Context $ctx -Prefix $Dataset
    $NumBytes = 0
    $BlobList | ForEach-Object {$NumBytes = $NumBytes + $_.Length}

    "Expected bytes to read: $NumBytes" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    return $NumBytes
}

function Compare-BytesRead {
    param (
        [Parameter(Mandatory=$true)]
        [string]$LogFile,

        [Parameter(Mandatory=$true)]
        [long]$ExpectedBytesToRead
    )

    if (-not(Get-Content $LogFile | Select-String -Pattern "Bytes read")) {
        "I/O stats summary log not found in $LogFile" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return $false 
    }

    $BytesRead = ((Get-Content $LogFile | Select-String -Pattern "Bytes read: [0-9]+").Matches.Value | Select-String -Pattern "[0-9]+").Matches.Value
    if ([long]$BytesRead -ne $ExpectedBytesToRead) {
        "Failed IO stats check - Bytes read: $BytesRead, Expected bytes to read: $ExpectedBytesToRead" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return $false
    }

    return $true
}

[Flags()] enum VerificationStatus
{
    Success = 1
    NoncriticalFailure = 2
    CriticalFailure = 4
}

function Test-PodExecution {
    param (
        [Parameter(Mandatory=$true)]
        [string]$LogDir,

        [Parameter(Mandatory=$true)]
        [string]$ClientName,

        [Parameter(Mandatory=$true)]
        [bool]$VerifyIoStats,

        [Parameter(Mandatory=$true)]
        [long]$ExpectedBytesToRead
    )

    $Pattern = [regex] "stress-client-${ClientName}-[a-zA-Z0-9\-]*"
    $PodList = kubectl --kubeconfig $KubeConfig get pods | Where-Object {$_ -match $Pattern} | ForEach-Object {($_ -split "\s+")[0]}
    $IOStatsVerified = $true
    $NoErrorsFound = $true
    $AllowedErrorStrings="Couldn't resolve server address"

    foreach ($Pod in $PodList) {
        $LogFile = Join-Path $LogDir "${Pod}.log"
        kubectl --kubeconfig $KubeConfig logs $Pod > $LogFile
    }
    foreach ($Pod in $PodList) {
        if($VerifyIoStats) {
            $LogFile = Join-Path $LogDir "${Pod}.log"
            $IOStatsVerified = $IOStatsVerified -and (Compare-BytesRead -LogFile $LogFile -ExpectedBytesToRead $ExpectedBytesToRead)
        }
        
        if (Get-Content $LogFile | Select-String "DACS_CTR:(ERROR|CRITICAL)" | Select-String $AllowedErrorStrings -NotMatch -Quiet) {
            "Found DACS_CTR:ERROR or DACS_CTR:CRITICAL in $LogFile" `
                | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            $NoErrorsFound = $NoErrorsFound -and $false
        }
        else {
            "No DACS_CTR:ERROR or DACS_CTR:CRITICAL found in $LogFile" `
                | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        }
    }

    if ($NoErrorsFound -and $IOStatsVerified) {
        "Client(s) with name: ${ClientName} have executed successfully..." `
            | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return [VerificationStatus]::Success
    }
    elseif (-not($NoErrorsFound) -and $IOStatsVerified) {
        "Client(s) with name: ${ClientName} have encountered non-critical failures..." `
            | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        return [VerificationStatus]::NoncriticalFailure
    }

    "Client(s) with name: ${ClientName} have encountered critical failures..." `
        | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    return [VerificationStatus]::CriticalFailure
}

function Start-Test {
    # Accept a mandatory parameter which is a json object
    param (
        [Parameter(Mandatory=$true)]
        $TestArgs
    )

    $TestName = $TestArgs.TestName
    "Starting test scenario - $TestName" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    # $CurrentTs = Get-Date -u +"%Y-%m-%dT%H:%M:%SZ"
    $SleepInterval = $TestArgs.SleepInterval
    $ClientYamlFiles = Get-ClientConfigs -TestArgs $TestArgs
    $ClientYamlFiles = $ClientYamlFiles -split ' '
    $ExpectedBytesRead = @()

    for ($counter = 0; $counter -lt $ClientYamlFiles.Length; $counter++) {
        $Value = Get-ExpectedBytesToRead -Dataset $TestArgs.ClientConfigs[$counter].Dataset -TestArgs $TestArgs
        $ExpectedBytesRead += $Value
        $Name = $TestArgs.ClientConfigs[$counter].Name

        "Client(s): $Name will read $Value bytes" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
    }

    $Iteration = 1
    $NumSuccess = 0
    $NumNonCritical = 0
    $NumCritical = 0

    while ($true) {
        # Start the cache client(s)
        for($counter = 0; $counter -lt $ClientYamlFiles.Length; $counter++) {
            $YamlFile = $ClientYamlFiles[$counter]
            $Name = $TestArgs.ClientConfigs[$counter].Name
            $ClientIdentifier = "stress-client-$Name"
            if(-not(Start-Client -YamlFile $YamlFile -ClientIdentifier $ClientIdentifier)) {
                "Failed to start client(s) with identifier: $ClientIdentifier" `
                    | Write-LogOutput -LogFile $Script:TestSuiteLogFile
                return
            }
        }
        
        # Wait for all cache-client pods to complete
        for($counter = 0; $counter -lt $ClientYamlFiles.Length; $counter++) {
            $Name = $TestArgs.ClientConfigs[$counter].Name
            $ClientIdentifier = "stress-client-$Name"
            if (-not(Test-PodsCompleted -Identifier $ClientIdentifier)) {
                "Client(s) with identifier: $ClientIdentifier failed to complete..." `
                    | Write-LogOutput -LogFile $Script:TestSuiteLogFile
                return
            }
        }

        "All clients have completed successfully" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        "Verifying that all clients have executed correctly..." | Write-LogOutput -LogFile $Script:TestSuiteLogFile

        $LogDir = Join-Path $OutputDir "logs"
        $LogDir = Join-Path $LogDir $Iteration
        New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

        [VerificationStatus]$IterationStatus = [VerificationStatus]::Success
        for($counter = 0; $counter -lt $ClientYamlFiles.Length; $counter++) {
            $Name = $TestArgs.ClientConfigs[$counter].Name
            "Verifying client(s) with name: $Name" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            $Value = Test-PodExecution -LogDir $LogDir -ClientName $Name -VerifyIoStats $TestArgs.ClientConfigs[$counter].IOVerification -ExpectedBytesToRead $ExpectedBytesRead[$counter]
            $IterationStatus = $IterationStatus -bor $Value
        }

        # Capture server logs since last ts at this point
        $ServerPodList = kubectl --kubeconfig $KubeConfig get pods | Where-Object {$_ -match "cacheserver-[a-zA-Z0-9\-]*"} | ForEach-Object {($_ -split "\s+")[0]}
        foreach ($Pod in $ServerPodList) {
            $LogFile = Join-Path $LogDir "${Pod}.log"
            kubectl --kubeconfig $KubeConfig logs $Pod > $LogFile # need to add since-time when i get it to work

            if (Get-Content $LogFile | Select-String "DACS_CTR:(ERROR|CRITICAL)" | Select-String "No IB devices found" -NotMatch -Quiet) {
                "Found DACS_CTR:ERROR or DACS_CTR:CRITICAL in $LogFile" `
                    | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            }
            else {
                "No DACS_CTR:ERROR or DACS_CTR:CRITICAL found in $LogFile" `
                    | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            }
        }

        if (($IterationStatus -band [VerificationStatus]::CriticalFailure) -eq [VerificationStatus]::CriticalFailure) {
            $NumCritical++
            "Iteration $Iteration completed with critical failures" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        }
        elseif (($IterationStatus -band [VerificationStatus]::NoncriticalFailure) -eq [VerificationStatus]::NoncriticalFailure) {
            $NumNonCritical++
            "Iteration $Iteration completed with non-critical failures" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        }
        else {
            $NumSuccess++
            "Iteration $Iteration completed successfully" | Write-LogOutput -LogFile $Script:TestSuiteLogFile
        }

        # Delete the cache-client pods to prepare for next iteration
        foreach ($ClientYamlFile in $ClientYamlFiles) {
            Stop-Client -YamlFile $ClientYamlFile
        }

        "Finished $Iteration runs. SuccessfulRuns=$NumSuccess NonCriticalFailures=$NumNonCritical CriticalFailures=$NumCritical" `
            | Write-LogOutput -LogFile $Script:TestSuiteLogFile

        $Iteration += 1
        if ($SleepInterval -gt 0) {
            "Sleeping for $SleepInterval seconds before next iteration..." | Write-LogOutput -LogFile $Script:TestSuiteLogFile
            Start-Sleep -Seconds $SleepInterval
        }

        # $CurrentTs = Get-Date -u +"%Y-%m-%dT%H:%M:%SZ" # not working currently, can fix later
    }
}

#########################################################################################
################################## Script Entry Point ###################################
#########################################################################################
if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
$LogsDir = Join-Path $OutputDir "logs"

New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

# Read the test config into a json object
$TestArgs = Get-Content $TestConfigFile -Raw | ConvertFrom-Json
$TestSuiteLogFile = Join-Path $LogsDir "test.log"
try
{
    Start-Test -TestArgs $TestArgs
}
finally {
    "Exiting test..." | Write-LogOutput -LogFile $TestSuiteLogFile
    $ExitDir = Join-Path $LogsDir "exit"
    New-Item -ItemType Directory -Force -Path $ExitDir | Out-Null

    $Pattern = [regex] "stress-client-[a-zA-Z0-9\-]*"
    $PodList = kubectl --kubeconfig $KubeConfig get pods | Where-Object {$_ -match $Pattern} | ForEach-Object {($_ -split "\s+")[0]}

    foreach ($Pod in $PodList) {
        $LogFile = Join-Path $ExitDir "${Pod}.log"
        kubectl --kubeconfig $KubeConfig logs $Pod > $LogFile
    }

    $SetupDir = Join-Path $OutputDir "setup"
    $Files = @()

    # No need to recurse when scanning for yaml files
    Get-ChildItem -Path $SetupDir | ForEach-Object {$Files += $_.FullName}
    foreach($YamlFile in $Files) {
        kubectl --kubeconfig $KubeConfig delete -f $YamlFile
    }
}