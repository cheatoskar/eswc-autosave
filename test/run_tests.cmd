@echo off
rem Runs every test after build.cmd. Optional: the folder holding copies of the three game exes (read only), e.g.
rem   test\run_tests.cmd C:\path\to\exes   (TmNationsESWC.exe, TmSunrise.exe, TMORIGINAL.EXE)
rem onlinetest.exe (a real HTTPS request to the ESWC server) is not run here; run it by hand when needed.
setlocal
cd /d "%~dp0.."
set "T=build\test"
set "FAIL=0"
%T%\essentials_test.exe || set "FAIL=1"
%T%\hp_tests.exe || set "FAIL=1"
%T%\menu_test.exe || set "FAIL=1"
if "%~1"=="" (
    %T%\legacy_test.exe || set "FAIL=1"
    echo SKIP  realexe_test: no exe folder given
) else (
    %T%\legacy_test.exe "%~1\TmSunrise.exe" sunrise "%~1\TMORIGINAL.EXE" original "%~1\TmNationsESWC.exe" eswc || set "FAIL=1"
    %T%\realexe_test.exe "%~1\TmNationsESWC.exe" eswc "%~1\TmSunrise.exe" sunrise "%~1\TMORIGINAL.EXE" original || set "FAIL=1"
)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\sync-horizon.ps1 -Check || set "FAIL=1"
if "%FAIL%"=="1" (echo SOME TESTS FAILED& exit /b 1)
echo ALL TESTS PASSED
exit /b 0
