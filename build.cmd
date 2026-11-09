@echo off
rem Builds dist\dinput8.dll (32-bit) with the MSVC Build Tools, plus the online-fix test exe.
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if not exist "%VCVARS%" goto novc
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if not exist build\test mkdir build\test
if not exist dist mkdir dist
cl /nologo /c /O2 /MT /GS /W4 /WX /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"build\\" src\proxy.cpp src\autosave.cpp src\online.cpp src\log.cpp src\sha256.cpp || exit /b 1
link /nologo /DLL /MACHINE:X86 /SAFESEH /DYNAMICBASE /NXCOMPAT /DEF:src\exports.def /OUT:dist\dinput8.dll /IMPLIB:build\dinput8.lib build\proxy.obj build\autosave.obj build\online.obj build\log.obj build\sha256.obj user32.lib kernel32.lib bcrypt.lib wininet.lib || exit /b 1
copy /y eswc_autosave.ini dist\eswc_autosave.ini >nul
cl /nologo /O2 /MT /W4 /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"build\test\\" /Fe"build\test\onlinetest.exe" test\onlinetest.cpp src\online.cpp src\log.cpp wininet.lib >nul || exit /b 1
echo built dist\dinput8.dll and build\test\onlinetest.exe
exit /b 0
:novc
echo MSVC vcvarsamd64_x86.bat not found
exit /b 1
