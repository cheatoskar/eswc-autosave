@echo off
rem Builds Legacy Essentials, 32-bit, with the MSVC Build Tools:
rem   dist\dinput8.dll        the small loader (no C runtime)
rem   dist\eswc_autosave.dll  the mod: autosave, TMX copy, login fix, controller fix, menu
rem   build\test\*.exe        tests (run them with test\run_tests.cmd)
rem Code shared with the Horizon Client comes from vendor\horizon\ (tools\sync-horizon.ps1).
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if not exist "%VCVARS%" goto novc
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
for %%d in (build build\test build\imgui build\horizon build\mod build\menu_test dist) do if not exist %%d mkdir %%d
set "DEFS=/DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX"
set "CXX=cl /nologo /c /O2 /MT /GS /W4 /WX /EHsc /std:c++17 /utf-8 %DEFS%"

rem loader: DirectInput pass-through (to dinput8.chain.dll or the system DLL) + loads eswc_autosave.dll, nothing
rem else. No C runtime, so the DLL holds only the code in loader.cpp (a few KB; imports: kernel32, user32).
cl /nologo /c /O1 /GS- /W4 /WX /EHs-c- /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Fo"build\\" src\loader.cpp || exit /b 1
link /nologo /DLL /MACHINE:X86 /SAFESEH /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB /ENTRY:DllMain /DEF:src\loader.def /OUT:dist\dinput8.dll /IMPLIB:build\dinput8.lib build\loader.obj user32.lib kernel32.lib || exit /b 1

rem Dear ImGui (the Horizon Client's copy), as the client builds it. Built once; delete build\ after a sync.
if exist build\imgui.lib goto imguidone
cl /nologo /c /O2 /MT /GS /W3 /EHsc /std:c++17 /utf-8 %DEFS% /Ivendor\horizon\imgui /Fo"build\imgui\\" vendor\horizon\imgui\imgui.cpp vendor\horizon\imgui\imgui_draw.cpp vendor\horizon\imgui\imgui_tables.cpp vendor\horizon\imgui\imgui_widgets.cpp vendor\horizon\imgui\backends\imgui_impl_dx9.cpp vendor\horizon\imgui\backends\imgui_impl_win32.cpp || exit /b 1
lib /nologo /OUT:build\imgui.lib build\imgui\*.obj || exit /b 1
:imguidone

rem the Horizon Client's login fix and controller fix, unchanged (onlinefix.cpp gets src\shim\host\host.h)
%CXX% /Isrc\shim /Fo"build\horizon\\" vendor\horizon\host\onlinefix.cpp vendor\horizon\controller-hotplug\hp_tracker.cpp vendor\horizon\controller-hotplug\hp_enum.cpp vendor\horizon\controller-hotplug\hp_bind.cpp vendor\horizon\controller-hotplug\hp_game.cpp vendor\horizon\controller-hotplug\hp_watch.cpp vendor\horizon\controller-hotplug\hp_service.cpp || exit /b 1

rem the mod
%CXX% /Ivendor\horizon\imgui /Fo"build\mod\\" src\main.cpp src\autosave.cpp src\legacy.cpp src\hook.cpp src\header.cpp src\log.cpp src\sha256.cpp src\tmxcopy.cpp src\settings.cpp src\coexist.cpp src\controller.cpp src\essentials.cpp src\overlay.cpp || exit /b 1
link /nologo /DLL /OPT:REF /OPT:ICF /MACHINE:X86 /SAFESEH /DYNAMICBASE /NXCOMPAT /DEF:src\eswc_autosave.def /OUT:dist\eswc_autosave.dll /IMPLIB:build\eswc_autosave.lib build\mod\*.obj build\horizon\*.obj build\imgui.lib user32.lib kernel32.lib gdi32.lib shell32.lib ole32.lib bcrypt.lib wininet.lib dxguid.lib setupapi.lib || exit /b 1
copy /y eswc_autosave.ini dist\eswc_autosave.ini >nul

rem tests
set "TCXX=cl /nologo /O2 /MT /W4 /EHsc /std:c++17 /utf-8 %DEFS%"
%TCXX% /Isrc\shim /Fo"build\test\\" /Fe"build\test\onlinetest.exe" test\onlinetest.cpp vendor\horizon\host\onlinefix.cpp src\log.cpp wininet.lib >nul || exit /b 1
%TCXX% /WX /Fo"build\test\\" /Fe"build\test\tmxcopy_test.exe" test\tmxcopy_test.cpp src\tmxcopy.cpp || exit /b 1
rem Sunrise/Original autosave against a fake game image
%TCXX% /WX /Fo"build\test\\" /Fe"build\test\legacy_test.exe" test\legacy_test.cpp src\legacy.cpp src\hook.cpp src\header.cpp src\log.cpp src\sha256.cpp user32.lib bcrypt.lib || exit /b 1
rem the Horizon Client's controller-fix unit tests, on the vendored code
%TCXX% /WX /Fo"build\test\\" /Fe"build\test\hp_tests.exe" vendor\horizon\controller-hotplug\tests\hp_tests.cpp build\horizon\hp_*.obj user32.lib dxguid.lib setupapi.lib || exit /b 1
rem the vendored code against the real exes on disk (read only): realexe_test.exe <exe> eswc|sunrise|original ...
%TCXX% /WX /Fo"build\test\\" /Fe"build\test\realexe_test.exe" test\realexe_test.cpp build\horizon\hp_*.obj user32.lib dxguid.lib setupapi.lib /link /BASE:0x30000000 || exit /b 1
rem settings, menu key, Horizon Client coexistence plan
%TCXX% /WX /Fo"build\test\\" /Fe"build\test\essentials_test.exe" test\essentials_test.cpp src\settings.cpp src\coexist.cpp || exit /b 1
rem the real menu drawn by the real overlay on a Direct3D 9 device of a test window (writes menu_test.bmp)
%CXX% /Ivendor\horizon\imgui /Fo"build\menu_test\\" test\menu_test.cpp || exit /b 1
link /nologo /MACHINE:X86 /OUT:build\test\menu_test.exe build\menu_test\menu_test.obj build\mod\essentials.obj build\mod\overlay.obj build\mod\settings.obj build\mod\coexist.obj build\mod\controller.obj build\mod\log.obj build\horizon\*.obj build\imgui.lib user32.lib kernel32.lib gdi32.lib shell32.lib ole32.lib wininet.lib dxguid.lib setupapi.lib d3d9.lib dinput8.lib || exit /b 1
echo built dist\dinput8.dll, dist\eswc_autosave.dll and the tests in build\test
exit /b 0
:novc
echo MSVC vcvarsamd64_x86.bat not found
exit /b 1
