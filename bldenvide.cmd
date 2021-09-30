@echo off
call "%~dp0.pipelines\setupenv.cmd"
@echo Enabling Powershell Execution
powershell Set-ExecutionPolicy -ExecutionPolicy Unrestricted -Scope CurrentUser
@echo Syncing build environment ...
msbuild -t:Restore -noConsoleLogger
@echo Generating/Updating Visual Studio Solution
msbuild -t:SlnGen -noConsoleLogger /Property:"SlnGenSolutionFileFullPath=sln\dirs.sln"