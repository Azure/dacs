param (
    [Parameter(Mandatory=$true)]
    [string]
    $inputhashFile,

    [Parameter(Mandatory=$true)]
    [string]
    $inputhashdeleteFile,

    [Parameter(Mandatory=$true)]
    [string]
    $outputhashFilterFile
)


$outputhashFilterFileContents = Get-Content -Path "$inputhashFile" 

if (Test-Path -Path "$inputhashdeleteFile") {
    $outputhashFilterFileContents = $outputhashFilterFileContents | Where-Object { -Not (Select-String -Path "$inputhashdeleteFile" -Pattern $_) }
    Write-Host $outputhashFilterFileContents
}

$outputhashFilterFileContents | Set-Content -Path "$outputhashFilterFile"
