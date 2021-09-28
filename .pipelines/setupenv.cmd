@REM Initialize the developer environment and run self-check on the
@REM initialized environment

@IF NOT "%DevEnvDir%" == "" exit /b 0

@FOR /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -products * -prerelease -latest -property installationPath`) do (
  IF exist "%%i\Common7\Tools\vsdevcmd.bat" (
    call "%%i\Common7\Tools\vsdevcmd.bat" -arch=amd64 -host_arch=amd64
  )
)