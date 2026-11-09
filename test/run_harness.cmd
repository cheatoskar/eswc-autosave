@echo off
rem Builds the 32-bit harness and runs it against a copy of dist\dinput8.dll in build\harness\.
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
call "%VCVARS%" >nul 2>nul || exit /b 1
cd /d "%~dp0.."
if not exist build\harness mkdir build\harness
del /q build\harness\eswc_autosave.log 2>nul
copy /y dist\dinput8.dll build\harness\dinput8.dll >nul
cl /nologo /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE test\harness.cpp /Fo"build\harness\\" /Fe"build\harness\harness.exe" user32.lib ole32.lib >nul || exit /b 1
build\harness\harness.exe "%CD%\build\harness\dinput8.dll"
echo exit=%ERRORLEVEL%
echo ---- log ----
type build\harness\eswc_autosave.log
