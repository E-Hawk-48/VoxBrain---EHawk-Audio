@echo off
set OUT=%~dp0..\diag.txt
echo DIAG START > "%OUT%"
echo %DATE% %TIME% >> "%OUT%"
ver >> "%OUT%" 2>&1
echo --- powershell check --- >> "%OUT%"
where powershell >> "%OUT%" 2>&1
powershell -NoProfile -Command "Write-Output ('PS OK ' + $PSVersionTable.PSVersion)" >> "%OUT%" 2>&1
echo --- cmake check --- >> "%OUT%"
where cmake >> "%OUT%" 2>&1
cmake --version >> "%OUT%" 2>&1
echo --- VS 2022 folders --- >> "%OUT%"
dir "C:\Program Files\Microsoft Visual Studio\2022" /b >> "%OUT%" 2>&1
dir "C:\Program Files (x86)\Microsoft Visual Studio\2022" /b >> "%OUT%" 2>&1
echo --- VS cmake --- >> "%OUT%"
dir "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" /b >> "%OUT%" 2>&1
echo --- git check --- >> "%OUT%"
where git >> "%OUT%" 2>&1
echo --- msbuild check --- >> "%OUT%"
dir "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" /b >> "%OUT%" 2>&1
echo DIAG END >> "%OUT%"
echo.
echo Done! Report written to diag.txt in the VocalForge folder.
echo You can close this window and tell Claude it finished.
pause
