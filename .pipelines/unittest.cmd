@SETLOCAL EnableDelayedExpansion
@SET TEST_RESULT_FOLDER=%~dp0TestResults

@SET BUILD_CONFIGURATION=Debug
@SET TEST_RUNSETTINGS=.runsettings
@IF NOT .%1.==.. SET BUILD_CONFIGURATION=%1
@IF NOT .%2.==.. SET TEST_RUNSETTINGS=%2

@SET EX=0

@REM Initialize the developer environment and run self-check on the
@REM initialized environment
@echo Initializing test environment
@call %~dp0setupenv.cmd

@SET EX=%ERRORLEVEL%
@IF "%EX%" neq "0" (
    @ECHO Init.cmd FAILED. 1>&2
    @goto exit
)

@REM Save working directory so that we can return after the build.
@pushd "%~dp0.."

@REM Login to azure container repo
@echo Logging in to dacs
@call az acr login -n dacs

@REM Prune stopped containers
@echo Pruning stopped containers
@call docker container prune --force

@echo off
set UNIT_TEST_DLLS=

@for /F %%i in ('dir /s /b bin\%BUILD_CONFIGURATION%\*tests*.dll') do (

    call set "testDLL=%%i"
    call set "UNIT_TEST_DLLS=%%UNIT_TEST_DLLS%% !testDLL!,"
)

@IF "%UNIT_TEST_DLLS%" == "" (
    @ECHO "No tests found. Exiting." 1>&2
    @exit /b 0
)

set UNIT_TEST_DLLS=%UNIT_TEST_DLLS:~0,-1%

FOR /F %%i in ('dir /s /b bin\Test-With-Retry.ps1') do ( SET TEST_SCRIPT=%%i)
@echo on

@echo Running Unit Tests
@echo %TEST_SCRIPT% -TestAssemblies %UNIT_TEST_DLLS% -TestFilters "(TestCategory=Unit|TestCategory=Container)" -RunSettingsPath %TEST_RUNSETTINGS% -TestResultsPath %TEST_RESULT_FOLDER%
@pwsh -Command "& %TEST_SCRIPT% -TestAssemblies %UNIT_TEST_DLLS% -TestFilters ""(TestCategory=Unit|TestCategory=Container)"" -RunSettingsPath %TEST_RUNSETTINGS% -TestResultsPath %TEST_RESULT_FOLDER%"
@SET EX=%ERRORLEVEL%

@REM Restore working directory of user so this works fine in dev box.
@popd

@REM Exit with last seen exit code
@exit /b %EX%
