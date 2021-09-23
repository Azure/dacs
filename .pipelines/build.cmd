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

@SET SKIP_NATIVE_BUILD_PARAM=""
@IF "%TF_BUILD%" == "True" (
    @SET SKIP_NATIVE_BUILD_PARAM="-p:SKIP_NATIVE_BUILD=TRUE"
)

@REM Save working directory so that we can return after the build.
@pushd "%~dp0.."

@REM Build the code.
msbuild "dirs.proj" -maxCpuCount -p:TreatWarningsAsErrors=true %SKIP_NATIVE_BUILD_PARAM% -p:Configuration=%BUILD_CONFIGURATION% -v:n -bl:%BUILD_RESULT_FOLDER%\msbuild.build.%BUILD_CONFIGURATION%.binlog

@SET EX=%ERRORLEVEL%
@IF "%EX%" neq "0" (
    @ECHO Build FAILED. 2>&1
    @goto exit
)

@ECHO Validate no source files modified. Any files found sent to stderr
git clean -x -d -n src 1>&2


:exit
@REM Restore working directory of user so this works fine in dev box.
@popd

@REM Exit with last seen exit code
exit /b %EX%
