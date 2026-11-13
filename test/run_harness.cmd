@echo off
rem Builds the 32-bit harness and runs it against copies of the dist DLLs in build\harness\:
rem   1. dinput8.dll + eswc_autosave.dll   -> DirectInput works, the log says "UNKNOWN exe ... autosave off"
rem   2. dinput8.dll alone                 -> DirectInput still works, no log is written
rem   3. dinput8.dll + dinput8.chain.dll   -> the chained DLL (another copy of the loader here) is loaded and
rem                                           DirectInput goes through it; the mod starts once
rem   4. our loader as dinput8.chain.dll under another dinput8.dll (Horizon's place for it): it finds itself as
rem      its own chain and uses the system DLL
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
call "%VCVARS%" >nul 2>nul || exit /b 1
cd /d "%~dp0.."
set "H=build\harness"
if not exist %H% mkdir %H%
cl /nologo /O2 /MT /W4 /EHsc /DUNICODE /D_UNICODE test\harness.cpp /Fo"%H%\\" /Fe"%H%\harness.exe" user32.lib ole32.lib >nul || exit /b 1
call :clean
copy /y dist\dinput8.dll %H%\dinput8.dll >nul
copy /y dist\eswc_autosave.dll %H%\eswc_autosave.dll >nul
echo ==== 1: loader + mod ====
%H%\harness.exe "%CD%\%H%\dinput8.dll"
echo exit=%ERRORLEVEL%
echo ---- log ----
type %H%\eswc_autosave.log
echo.
echo ==== 2: loader alone ====
del /q %H%\eswc_autosave.log %H%\eswc_autosave.dll 2>nul
%H%\harness.exe "%CD%\%H%\dinput8.dll"
echo exit=%ERRORLEVEL%
if exist %H%\eswc_autosave.log (echo UNEXPECTED: log written without the mod) else (echo no log, as expected)
echo.
echo ==== 3: loader + dinput8.chain.dll + mod ====
call :clean
copy /y dist\dinput8.dll %H%\dinput8.dll >nul
copy /y dist\dinput8.dll %H%\dinput8.chain.dll >nul
copy /y dist\eswc_autosave.dll %H%\eswc_autosave.dll >nul
%H%\harness.exe "%CD%\%H%\dinput8.dll"
echo exit=%ERRORLEVEL%
echo ---- log ----
type %H%\eswc_autosave.log
echo.
echo ==== 4: our loader loaded as dinput8.chain.dll ====
call :clean
copy /y dist\dinput8.dll %H%\dinput8.chain.dll >nul
copy /y dist\eswc_autosave.dll %H%\eswc_autosave.dll >nul
%H%\harness.exe "%CD%\%H%\dinput8.chain.dll"
echo exit=%ERRORLEVEL%
call :clean
exit /b 0

:clean
del /q %H%\eswc_autosave.log %H%\eswc_autosave.dll %H%\dinput8.dll %H%\dinput8.chain.dll 2>nul
exit /b 0
