@echo off
REM Build MOYU on Windows using clang-cl + MSVC linker (via vcvarsall).
setlocal enableextensions enabledelayedexpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
if not exist "%BUILD%" mkdir "%BUILD%"

set "VS_BUILDTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_COMMUNITY=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_PRO=C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_ENT=C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"

set "VCVARS="
if exist "%VS_BUILDTOOLS%" set "VCVARS=%VS_BUILTOOLS%"
if exist "%VS_BUILDTOOLS%" set "VCVARS=%VS_BUILDTOOLS%"
if not defined VCVARS if exist "%VS_COMMUNITY%" set "VCVARS=%VS_COMMUNITY%"
if not defined VCVARS if exist "%VS_PRO%" set "VCVARS=%VS_PRO%"
if not defined VCVARS if exist "%VS_ENT%" set "VCVARS=%VS_ENT%"

if not defined VCVARS (
    echo [build] ERROR: cannot find vcvarsall.bat under VS 2022.
    exit /b 1
)

call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo [build] vcvarsall failed
    exit /b 1
)

REM Gather source files. Use dir /b to get full file listing.
REM Exclude platform_linux.c and platform_macos.m on Windows build.
set "SRCS="
for /f "delims=" %%f in ('dir /b /s "%ROOT%\src\*.c"') do (
    echo %%f | findstr /i /c:"platform_linux" /c:"platform_macos" >nul || set "SRCS=!SRCS! "%%f""
)
set "LUAS="
for /f "delims=" %%f in ('dir /b /s "%ROOT%\third_party\lua\*.c"') do set "LUAS=!LUAS! "%%f""

clang-cl /nologo /std:c11 /O2 /D_CRT_SECURE_NO_WARNINGS ^
    /W3 /wd4100 /wd4189 /wd4244 /wd4267 /wd4068 ^
    /I"%ROOT%\src" /I"%ROOT%\third_party\lua" /I"%ROOT%\third_party\cjson" ^
    %SRCS% %LUAS% "%ROOT%\third_party\cjson\cJSON.c" ^
    /Fe:"%BUILD%\moyu.exe" /Fo:"%BUILD%\\" ^
    /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ws2_32.lib winhttp.lib

if errorlevel 1 (
    echo [build] compile/link failed
    exit /b 1
)

xcopy /E /I /Y /Q "%ROOT%\assets" "%BUILD%\assets" >nul
xcopy /E /I /Y /Q "%ROOT%\scripts" "%BUILD%\scripts" >nul

echo [build] OK -^> %BUILD%\moyu.exe
for %%I in ("%BUILD%\moyu.exe") do echo size: %%~zI bytes
endlocal
