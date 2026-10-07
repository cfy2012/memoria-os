@echo off
title Memoria OS - CHAT Proxy (8333)
cd /d "%~dp0"
echo.
echo  Memoria OS CHAT proxy -> http://127.0.0.1:8333/chat/api.php
echo  Keep this window open while using the simulator CHAT.
echo  Close this window to stop the proxy.
echo.
where py >nul 2>nul
if exist "%~dp0chat_proxy.exe" (
  "%~dp0chat_proxy.exe" 8333
) else if %errorlevel%==0 (
  py chat_proxy.py 8333
) else (
  python chat_proxy.py 8333
)
echo.
echo Proxy exited. Press any key to close.
pause >nul
