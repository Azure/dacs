@SET BUILD_RESULT_FOLDER=%~dp0Log
@SET BUILD_CONFIGURATION=Debug
@IF NOT .%1.==.. SET BUILD_CONFIGURATION=%1

@SET EX=0

@REM Initialize the developer environment and run self-check on the
@REM initialized environment
call %~dp0setupenv.cmd

@REM For now, init.cmd checks if the right version of .NET Core SDK is installed on the machine and installs it if SDK is missing.
@REM It can do other things going forward.
call %~dp0init.cmd

@SET EX=%ERRORLEVEL%
@IF "%EX%" neq "0" (
    @ECHO Init.cmd FAILED. 2>&1
    @goto exit
)

@IF "%CDP_DEFAULT_CLIENT_PACKAGE_PAT%" neq "" (
    @SET AZURE_DEVOPS_EXT_PAT=%CDP_DEFAULT_CLIENT_PACKAGE_PAT%
)

@REM Save working directory so that we can return after the build.
@pushd "%~dp0.."

@REM Restore with msbuild to force creation of project assets
msbuild "dirs.proj" -maxCpuCount -warnAsError -Restore /t:Restore -p:Configuration=%BUILD_CONFIGURATION% -v:m -fileLogger -fileLoggerParameters:"logfile=%BUILD_RESULT_FOLDER%\msbuild.restore.%BUILD_CONFIGURATION%.log
@SET EX=%ERRORLEVEL%

@IF "%EX%" neq "0" (
    @REM Restore attempt 2 with msbuild to force creation of project assets
    msbuild "dirs.proj" -maxCpuCount -warnAsError -Restore /t:Restore -p:Configuration=%BUILD_CONFIGURATION% -v:m -fileLogger -fileLoggerParameters:"logfile=%BUILD_RESULT_FOLDER%\msbuild.restore.%BUILD_CONFIGURATION%.2.log
    @SET EX=%ERRORLEVEL%

    @IF "%EX%" neq "0" (
        @ECHO "msbuild restore FAILED." 2>&1
        @goto exit
    )
)

:exit
@REM Restore working directory of user so this works fine in dev box.
@popd

@REM Exit with last seen exit code
exit /b %EX%

