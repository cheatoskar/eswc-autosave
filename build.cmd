@echo off
rem Builds dist\dinput8.dll (the small loader) and dist\eswc_autosave.dll (the mod), 32-bit, with the
rem MSVC Build Tools, plus the online-fix and TMX-copy test exes.
rem   build.cmd       release build: eswc_autosave.dll WITHOUT the TMX copy
rem   build.cmd tmx   eswc_autosave.dll with the TMX copy (TmxCopy in the ini), not released yet
setlocal
set "TMXDEF="
set "TMXOBJ="
if /i "%~1"=="tmx" (
  set "TMXDEF=/DESWC_TMXCOPY"
  set "TMXOBJ=build\tmxcopy.obj"
)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if not exist "%VCVARS%" goto novc
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if not exist build\test mkdir build\test
if not exist dist mkdir dist
rem loader: DirectInput pass-through + loads eswc_autosave.dll, nothing else. No C runtime, so the
rem DLL holds only the code in loader.cpp (a few KB; imports: kernel32, user32 for wsprintfW).
cl /nologo /c /O1 /GS- /W4 /WX /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"build\\" src\loader.cpp || exit /b 1
link /nologo /DLL /MACHINE:X86 /SAFESEH /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB /ENTRY:DllMain /DEF:src\loader.def /OUT:dist\dinput8.dll /IMPLIB:build\dinput8.lib build\loader.obj user32.lib kernel32.lib || exit /b 1
rem the mod
cl /nologo /c /O2 /MT /GS /W4 /WX /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN %TMXDEF% /Fo"build\\" src\main.cpp src\autosave.cpp src\online.cpp src\log.cpp src\sha256.cpp src\tmxcopy.cpp || exit /b 1
link /nologo /DLL /MACHINE:X86 /SAFESEH /DYNAMICBASE /NXCOMPAT /DEF:src\eswc_autosave.def /OUT:dist\eswc_autosave.dll /IMPLIB:build\eswc_autosave.lib build\main.obj build\autosave.obj build\online.obj build\log.obj build\sha256.obj %TMXOBJ% user32.lib kernel32.lib bcrypt.lib wininet.lib || exit /b 1
copy /y eswc_autosave.ini dist\eswc_autosave.ini >nul
cl /nologo /O2 /MT /W4 /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"build\test\\" /Fe"build\test\onlinetest.exe" test\onlinetest.cpp src\online.cpp src\log.cpp wininet.lib >nul || exit /b 1
cl /nologo /O2 /MT /W4 /WX /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"build\test\\" /Fe"build\test\tmxcopy_test.exe" test\tmxcopy_test.cpp src\tmxcopy.cpp || exit /b 1
if defined TMXDEF (echo eswc_autosave.dll WITH the TMX copy) else (echo eswc_autosave.dll without the TMX copy)
echo built dist\dinput8.dll, dist\eswc_autosave.dll, build\test\onlinetest.exe and build\test\tmxcopy_test.exe
exit /b 0
:novc
echo MSVC vcvarsamd64_x86.bat not found
exit /b 1
