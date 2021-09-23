@ECHO Ensuring dotnet sdk installed
@CALL %~dp0ensure-dotnet-sdk-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)

@ECHO Ensuring go installed
@CALL %~dp0ensure-go-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)

@ECHO Ensuring python installed
@CALL %~dp0ensure-python-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)

@ECHO Ensuring python Azure modules installed
@CALL %~dp0ensure-python-dev-test-modules-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)

@ECHO Ensuring wheel installed
@CALL %~dp0ensure-wheel-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)

@ECHO Ensuring powershell installed
@CALL %~dp0ensure-pwsh-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)
