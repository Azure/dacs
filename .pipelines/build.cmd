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

@REM Save working directory so that we can return after the build.
@pushd "%~dp0.."

@REM Build the code.
msbuild "dirs.proj" -maxCpuCount -p:TreatWarningsAsErrors=true -p:Configuration=%BUILD_CONFIGURATION% -v:n -bl:%BUILD_RESULT_FOLDER%\msbuild.build.%BUILD_CONFIGURATION%.binlog

@SET EX=%ERRORLEVEL%
@IF "%EX%" neq "0" (
    @ECHO Build FAILED. 2>&1
    @goto exit
)

@REM Rebuild the code to verify incremental build still working
@REM This is currently not working so disabling it
@REM msbuild "dirs.proj" -maxCpuCount -p:TreatWarningsAsErrors=true -p:Configuration=%BUILD_CONFIGURATION% -v:m -fileLogger -fileLoggerParameters:"logfile=%BUILD_RESULT_FOLDER%\msbuild.incremental.%BUILD_CONFIGURATION%.log"

@REM @SET EX=%ERRORLEVEL%
@REM @IF "%EX%" neq "0" (
@REM     @ECHO Incremental Build FAILED. 2>&1
@REM     @goto exit
@REM )

@REM find /C " .cs" %BUILD_RESULT_FOLDER%\msbuild.incremental.%BUILD_CONFIGURATION%.log
@REM @SET INCEX=%ERRORLEVEL%
@REM @IF "%INCEX%" neq "1" (
@REM     @SET EX=1
@REM     @ECHO Incremental Build Check FAILED. 2>&1
@REM         @goto exit
@REM )

@ECHO Validate no source files modified. Any files found sent to stderr
git clean -x -d -n src 1>&2


:exit
@REM Restore working directory of user so this works fine in dev box.
@popd

@REM Exit with last seen exit code
exit /b %EX%
