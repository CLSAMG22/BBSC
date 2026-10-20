@echo off
rem Builds the BBSC mod: bbsc.dll (injected into shadPS4) and bbsc-launch.exe (the injector).
rem Output: D:\BBSC\build\mod\ ; log: D:\BBSC\build\mod-build.log (ends with exit=N)
rem bbsc.dll = bbsc_mod.cpp + bbsc_hooks.cpp + bbsc_relay.cpp + the emulator build's own
rem src\core\bloodborne_re.cpp (BBSC_MOD), with mod\shim\ answering its emulator includes.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin;%PATH%"
set "SRC=%~dp0"
set "OUT=D:\BBSC\build\mod"
set "OBJ=D:\BBSC\build\mod\obj"
set "LOG=D:\BBSC\build\mod-build.log"
if not exist "%OBJ%" mkdir "%OBJ%"
set EXT=%SRC%..\externals
set FLAGS=/nologo /std:c++latest /O2 /MT /EHsc /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /DBBSC_MOD /DFMT_HEADER_ONLY /I "%SRC%shim" /I "%SRC%..\src" /I "%EXT%\fmt\include" /I "%EXT%\xbyak"
echo building > "%LOG%"
clang-cl %FLAGS% /LD "%SRC%bbsc_mod.cpp" "%SRC%bbsc_hooks.cpp" "%SRC%bbsc_relay.cpp" "%SRC%bbsc_channel.cpp" "%SRC%bbsc_http.cpp" "%SRC%bbsc_discord.cpp" "%SRC%..\src\core\bbsc_memscan.cpp" "%SRC%..\src\core\bloodborne_re.cpp" /Fo"%OBJ%\\" /Fe"%OUT%\bbsc.dll" /link ws2_32.lib winhttp.lib user32.lib >> "%LOG%" 2>&1 || goto fail
clang-cl %FLAGS% "%SRC%bbsc_launch.cpp" /Fo"%OBJ%\\" /Fe"%OUT%\bbsc-launch.exe" /link user32.lib comdlg32.lib /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup >> "%LOG%" 2>&1 || goto fail
echo exit=0 >> "%LOG%"
exit /b 0
:fail
echo exit=1 >> "%LOG%"
exit /b 1
