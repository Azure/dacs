@ECHO Ensuring dotnet sdk installed
@CALL %~dp0ensure-dotnet-sdk-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)

@ECHO Ensuring powershell installed
@CALL %~dp0ensure-pwsh-installed.cmd
IF '%ERRORLEVEL%' NEQ '0' (
  EXIT /b %ERRORLEVEL%
)
