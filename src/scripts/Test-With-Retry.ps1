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

#
# Vstest wrapper with retries to identify flaky tests.
#
param(
  # Vstest assemblies
  [Parameter(Mandatory = $true)]
  [string[]]
  $TestAssemblies,

  # Vstest TestFilters
  [Parameter(Mandatory = $true)]
  [string]
  $TestFilters,

  # Vstest runsettings filepath
  [Parameter(Mandatory = $true)]
  [string]
  $RunSettingsPath,

  # Vstest test results filepath
  [Parameter(Mandatory = $true)]
  [string]
  $TestResultsPath,

  # Max retries of failed tests
  [Parameter(Mandatory = $false)]
  [string]
  $RetryCount = "5",

  # Windows or Linux
  [Parameter(Mandatory = $false)]
  [switch]
  $Linux
)

trap {
    Write-Error ($_ | Format-List * -Force | Out-String)
  }
function Get-FailedTestAssemblies([String]$TestResultsFile, [String[]]$TestFailures, [String[]]$TestAssemblies) {
    [xml]$TestResultsXml = Get-Content $TestResultsFile
    [String[]]$FailedAssemblies = @()
    foreach ($Test in $TestResultsXml.TestRun.TestDefinitions.UnitTest)
    {
        if ($TestFailures.Contains($Test.name))
        {
            $Assembly = $TestAssemblies[$TestAssemblies.toLower().IndexOf($Test.storage)]
            if (! ($FailedAssemblies.Contains($Assembly)))
            {
                $FailedAssemblies += $Assembly
            }
        }
    }

    return $FailedAssemblies
}

function Get-FailedTestFilters([String[]]$TestFailures, [String]$TestFilters) {
    $FailedTestNames = ($TestFailures | ForEach-Object {"Name=$_"}) -join "|"
    $FailedTestFilters = "($TestFilters)&($FailedTestNames)"

    return $FailedTestFilters
}

function Get-TestFailures([String]$TestResultsFile)
{
    [xml]$TestResultsXml = Get-Content $TestResultsFile
    [String[]]$FailedTests = @()
    foreach ($Result in $TestResultsXml.TestRun.Results.UnitTestResult)
    {
        if ($Result.outcome.Equals("Failed"))
        {
            $FailedTests += $Result.testName
        }
    }

    return $FailedTests
}

function Format-XML ([xml]$xml) 
{ 
    $stringbuilder = New-Object System.Text.StringBuilder
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true

    $xmlWriter = [System.XML.XmlWriter]::Create($stringbuilder, $settings)
    $xml.WriteContentTo($xmlWriter) 
    $xmlWriter.Close()

    $xmlDoc = [System.Xml.XmlDocument]::new()
    $xmlDoc.PreserveWhitespace = $true
    Write-Output $stringbuilder.ToString()
}

function Test-XmlPath([xml] $xml, [String] $xpath)
{
    $filter = $xml.SelectSingleNode($xpath)
    if(!$filter)
    {
        return $false
    }
    else
    {
        return $true
    }
}

function Generate-Runsettings([String] $oldRunSettingsPath, [String] $newRunSettingsPath, [String] $newResultsPath, [String] $newTestsFilter)
{
    [xml]$RunSettingsXml = Get-Content $oldRunSettingsPath
    $config_xpath = '/RunSettings/RunConfiguration'
    if (!(Test-XmlPath $RunSettingsXml "$config_xpath"))
    {
        $RunConfiguration = $RunSettingsXml.CreateElement("RunConfiguration")

    }
    else
    {
        $RunConfiguration = $RunSettingsXml.SelectSingleNode($config_xpath)
    }

    # Update TestCaseFilter
    $testcase_xpath = '/RunSettings/RunConfiguration/TestCaseFilter'
    if (!(Test-XmlPath $RunSettingsXml "$testcase_xpath"))
    {
        $TestCaseFilter = $RunSettingsXml.CreateElement("TestCaseFilter")
    }
    else
    {
        $TestCaseFilter = $RunSettingsXml.SelectSingleNode($testcase_xpath)
    }

    $TestCaseFilter.InnerText = $newTestsFilter
    $RunConfiguration.AppendChild($TestCaseFilter)

    # Update Results Path
    $results_xpath = '/RunSettings/RunConfiguration/ResultsDirectory'
    if (!(Test-XmlPath $RunSettingsXml "$results_xpath"))
    {
        $ResultsDirectory = $RunSettingsXml.CreateElement("ResultsDirectory")
    }
    else
    {
        $ResultsDirectory = $RunSettingsXml.SelectSingleNode($results_xpath)
    }

    $ResultsDirectory.InnerText = $newResultsPath
    $RunConfiguration.AppendChild($ResultsDirectory)

    $RunSettingsXml.RunSettings.AppendChild($RunConfiguration)

    $datacollectors_xpath = '/RunSettings/DataCollectionRunSettings'
    if (Test-XmlPath $RunSettingsXml "$datacollectors_xpath")
    {
        $RunSettingsXml.RunSettings.DataCollectionRunSettings.DataCollectors.DataCollector[0].Configuration.ResultsDirectory = $newResultsPath
    }

    Format-XML $RunSettingsXml.OuterXml | Out-File $newRunSettingsPath
}

function GetTestResultSummary([String] $TestResultsFile)
{
    $total = 0
    $passed = 0
    $failed = 0
    $skipped = 0

    [String[]] $Failures = @()
    [xml]$ResultsXml = Get-Content $TestResultsFile
    foreach ($Result in $ResultsXml.TestRun.Results.UnitTestResult)
    {
        if ($Result.InnerResults.UnitTestResult.Length -gt 0)
        {
            foreach ($InnerResult in $Result.InnerResults.UnitTestResult)
            {
                $FullTestName = $Result.testName + $InnerResult.testName
                if ($InnerResult.outcome.Equals("Passed"))
                {
                    $passed++
                }
                elseif ($InnerResult.outcome.Equals("Failed"))
                {
                    $Failures += $FullTestName
                    $failed++
                }
                elseif ($InnerResult.outcome.Equals("NotExecuted"))
                {
                    $skipped++
                }

                $total++
            }
        }
        else
        {
            if ($Result.outcome.Equals("Passed"))
            {
                $passed++
            }
            elseif ($Result.outcome.Equals("Failed"))
            {
                $Failures += $Result.testName
                $failed++
            }
            elseif ($Result.outcome.Equals("NotExecuted"))
            {
                $skipped++
            }

            $total++
        }
    }

    return $total, $passed, $failed, $skipped, $Failures
}
function PrintFinalResultsSummary([String] $ParentResultsPath, [Int]$attemptCount, [datetime] $startTime)
{
    $dateTime = Get-Date
    $endTime = [datetime]::Now

    $AllResultsPaths = Get-ChildItem -Path $ParentResultsPath -Recurse -Filter *.trx | % {$_.FullName}

    $InitialResultsPath = Join-Path $ParentResultsPath "Tests-Attempt-0"
    $InitialResult = Get-ChildItem -Path $InitialResultsPath -Recurse -Filter *.trx | % {$_.FullName} |  select -last 1
    $InitialTotal, $InitialPassed, $InitialFailed, $InitialSkipped, $InitialFailureList = GetTestResultSummary $InitialResult

    $FinalResultsPath = Join-Path $ParentResultsPath "Tests-Attempt-$attemptCount"
    $FinalResult = Get-ChildItem -Path ($FinalResultsPath) -Recurse -Filter *.trx | % {$_.FullName} |  select -last 1
    $FinalTotal, $_, $_, $_, $FinalFailureList = GetTestResultSummary $FinalResult

    $Flaky = 0
    if ($FinalTotal -gt 0)
    {
        [String[]] $FlakyTests = Compare-Object -ReferenceObject $InitialFailureList -DifferenceObject $FinalFailureList | Where-Object {$_.SideIndicator -eq "<="} | Select-Object -ExpandProperty InputObject
        $Flaky = $FlakyTests.Length    
    }

    $TotalFailed = $InitialFailed - $Flaky

    if ($Flaky -gt 0)
    {
        Write-Host ""
        Write-Host "List of Flaky Tests:"
        Write-Host ($FlakyTests -join ",")
        Write-Host ""
    }

    Write-Host "Results Files: " ($AllResultsPaths -join "`n") -ForegroundColor White
    Write-Host ""
    Write-Host "Total tests: $InitialTotal" -ForegroundColor White
    Write-Host "     Passed: $InitialPassed" -ForegroundColor Green
    Write-Host "     Failed: $TotalFailed" -ForegroundColor Red
    Write-Host "      Flaky: $Flaky" -ForegroundColor Magenta
    Write-Host "    Skipped: $InitialSkipped" -ForegroundColor Yellow
    Write-Host ('Total Time:',($endTime - $startTime)) -ForegroundColor White
    Write-Host ""

    if ($TotalFailed -eq 0)
    {
        Write-Host "Unit Test PASSED at $dateTime." 
        exit 0
    }
    else
    {
        Write-Host "Unit Tests FAILED at $dateTime."
        exit 1
    }
}

function Run-Tests([String[]] $TestAssemblies, [String] $TestFilters, [String] $RunSettings, [String] $ResultsPath, [bool] $Linux)
{
    if ($Linux)
    {
        # Used by Linux (unittest.sh)
        $assemblyList = @()
        foreach ($TestAssembly in $TestAssemblies) {
            $assemblyList += "$TestAssembly"
        }
        Write-Host "dotnet test $assemblyList --filter `"$TestFilters`" --logger trx --logger console --blame --settings `"$RunSettings`" --results-directory `"$ResultsPath`" -v:d"
        dotnet test $assemblyList --filter "$TestFilters" --logger trx --logger console --blame --settings "$RunSettings" --results-directory "$ResultsPath" -v:d
    }
    else
    {
        # Used by Windows (unittest.cmd)
        $argList = @()
        foreach ($TestAssembly in $TestAssemblies) {
            $argList += "`"$TestAssembly`""
        }
        $argList += "--TestCaseFilter:`"$TestFilters`""
        $argList += "--logger:trx"
        $argList += "--logger:console"
        $argList += "--Blame"
        $argList += "--Settings:`"$RunSettings`""
        $argList += "--ResultsDirectory:`"$ResultsPath`""

        $vstestConsoleApps = @(Get-ChildItem -Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019" -File -Include "vstest.console.exe" -Recurse)
        $vstestConsoleApp = $vstestConsoleApps[0]
    
        Write-Host "$($vstestConsoleApp.FullName)" $argList
        & "$($vstestConsoleApp.FullName)" $argList
    }
}

function Retry-Tests([String[]] $TestAssemblies, [String] $TestFilters, [String] $TestRunSettings, [String] $PreviousResultPath, [String] $NextResultPath, [String] $ParentResultsPath, [bool] $Linux)
{
    Write-Host "Updating testCaseFilter with Failed Tests."
    $previousResultsFile = Get-ChildItem -Path $PreviousResultPath -Filter *.trx | % {$_.FullName} |  select -last 1 
    [String[]] $failedTests = Get-TestFailures $previousResultsFile
    [String[]] $failedTestAssemblies = Get-FailedTestAssemblies $previousResultsFile $failedTests $TestAssemblies
    $failedTestFilters = Get-FailedTestFilters $failedTests $TestFilters

    Write-Host "Updating retry runsettings file with new testCaseFilter."
    $runsettingsFileName = Get-Date -Format yyyy-MM-dd_HH_mm_ss
    $newRunSettingsPath = Join-Path -Path $ParentResultsPath -ChildPath "$runsettingsFileName.runsettings"
    Generate-Runsettings $TestRunSettings $newRunSettingsPath $nextResultPath $failedTestFilters | Out-Null

    Run-Tests $failedTestAssemblies "$failedTestFilters" "$newRunSettingsPath" "$NextResultPath" $Linux
}

$startTime = [datetime]::Now

if (! (Test-Path $TestResultsPath))
{
    New-Item -ItemType Directory -Force -Path $TestResultsPath
}
else
{
    Remove-Item $TestResultsPath\* -Recurse -Force
}

$runCount = 0
$ResultsPath = Join-Path $TestResultsPath "Tests-Attempt-0"

Run-Tests $TestAssemblies "$TestFilters" "$RunSettingsPath" "$ResultsPath" $Linux
if ($LASTEXITCODE -ne 0)
{
    Write-Host "Retrying failed tests to identify flaky tests..."
    $prevResultsPath = "$ResultsPath"
    for ($i=1; $i -le $RetryCount; $i++) {
        $RetryResultsPath = Join-Path $TestResultsPath "Tests-Attempt-$i"
        $runCount++
        Retry-Tests $TestAssemblies "$TestFilters" "$RunSettingsPath" "$prevResultsPath" "$RetryResultsPath" "$ResultsPath" $Linux
        if ($LASTEXITCODE -eq 0)
        {
            break
        }
        else
        {
            Write-Host "Retry $i Failed. Results at $RetryResultsPath"
            $prevResultsPath = "$RetryResultsPath"
        }
    }
}

PrintFinalResultsSummary $TestResultsPath $runCount $startTime