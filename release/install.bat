@echo off
REM Kodas v0.16.0 Windows installer
setlocal
set SRC=%~dp0
set DST=%LOCALAPPDATA%\kodas
echo ==^> Installing Kodas to %DST%
if not exist %DST%\bin mkdir %DST%\bin
if not exist %DST%\lib\kodas mkdir %DST%\lib\kodas
copy /Y %SRC%bin\kodas.exe %DST%\bin\kodas.exe >nul
xcopy /E /Y /Q %SRC%lib\kodas %DST%\lib\kodas\ >nul
echo ==^> Adding %DST%\bin to user PATH
setx PATH "%PATH%;%DST%\bin" >nul
echo.
echo   Kodas installed. Open a new terminal and run: kodas version
echo.
pause
