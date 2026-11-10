@echo off
rem Builds the 32-bit harness and runs it twice against copies of the dist DLLs in build\harness\:
rem   1. dinput8.dll + eswc_autosave.dll  -> DirectInput works, the log says "UNKNOWN exe ... autosave off"
rem   2. dinput8.dll alone                -> DirectInput still works, no log is written
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
call "%VCVARS%" >nul 2>nul || exit /b 1
cd /d "%~dp0.."
if not exist build\harness mkdir build\harness
del /q build\harness\eswc_autosave.log build\harness\eswc_autosave.dll 2>nul
copy /y dist\dinput8.dll build\harness\dinput8.dll >nul
copy /y dist\eswc_autosave.dll build\harness\eswc_autosave.dll >nul
cl /nologo /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE test\harness.cpp /Fo"build\harness\\" /Fe"build\harness\harness.exe" user32.lib ole32.lib >nul || exit /b 1
echo ==== 1: loader + mod ====
build\harness\harness.exe "%CD%\build\harness\dinput8.dll"
echo exit=%ERRORLEVEL%
echo ---- log ----
type build\harness\eswc_autosave.log
echo.
echo ==== 2: loader alone ====
del /q build\harness\eswc_autosave.log build\harness\eswc_autosave.dll 2>nul
build\harness\harness.exe "%CD%\build\harness\dinput8.dll"
echo exit=%ERRORLEVEL%
if exist build\harness\eswc_autosave.log (echo UNEXPECTED: log written without the mod) else (echo no log, as expected)
