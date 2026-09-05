@echo off
setlocal
cd /d "%~dp0"

set DEFAULT_PATH=C:\Games\Wuthering Waves Game\Client\Content

if exist "keys.json" (
    set AES_ARG=-aes=keys.json
) else (
    set AES_ARG=
)

if exist "%DEFAULT_PATH%" (
    echo Launching UModel for Wuthering Waves: %DEFAULT_PATH%
    start "" umodel_64.exe -game=wuwa -path="%DEFAULT_PATH%" %AES_ARG%
) else (
    echo Launching UModel GUI for Wuthering Waves...
    start "" umodel_64.exe -game=wuwa %AES_ARG%
)
