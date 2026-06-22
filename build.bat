@echo off
REM Build MOYU, three reference MCP servers, and the test runner on Windows.
setlocal enableextensions enabledelayedexpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD=%ROOT%\build"
if not exist "%BUILD%" mkdir "%BUILD%"

set "VCVARS="
for %%V in (
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
) do if not defined VCVARS if exist %%V set "VCVARS=%%~V"

if not defined VCVARS (
  echo [build] ERROR: cannot find Visual Studio 2022 vcvarsall.bat
  exit /b 1
)
call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 exit /b 1

set "SRCS="
for /f "delims=" %%f in ('dir /b /s "%ROOT%\src\*.c"') do (
  echo %%f | findstr /i /c:"platform_linux" /c:"platform_macos" >nul || set "SRCS=!SRCS! "%%f""
)
set "LUAS="
for /f "delims=" %%f in ('dir /b /s "%ROOT%\third_party\lua\*.c"') do set "LUAS=!LUAS! "%%f""

REM Compile the primary target first. Its dependency objects are then reused by
REM the small test/reference targets, so clean builds never depend on stale .obj.
clang-cl /nologo /utf-8 /std:c11 /O2 /D_CRT_SECURE_NO_WARNINGS ^
  /W3 /wd4100 /wd4189 /wd4244 /wd4267 /wd4068 ^
  /clang:-Wno-unused-function /clang:-Wno-unused-variable ^
  /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_DEFAULT_MEMSTATUS=0 ^
  /I"%ROOT%\src" /I"%ROOT%\third_party\lua" /I"%ROOT%\third_party\cjson" /I"%ROOT%\third_party\sqlite" ^
  %SRCS% %LUAS% "%ROOT%\third_party\cjson\cJSON.c" "%ROOT%\third_party\sqlite\sqlite3.c" ^
  /Fe:"%BUILD%\moyu.exe" /Fo:"%BUILD%\\" ^
  /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ole32.lib crypt32.lib advapi32.lib ws2_32.lib winhttp.lib
if errorlevel 1 goto :main_failed

clang-cl /nologo /utf-8 /std:c11 /O2 /D_CRT_SECURE_NO_WARNINGS /W3 /wd4100 /wd4189 /wd4244 /wd4267 ^
  /I"%ROOT%\src" /I"%ROOT%\third_party\cjson" /I"%ROOT%\third_party\sqlite" ^
  "%ROOT%\tools\openchat.c" "%BUILD%\hash.obj" "%BUILD%\log.obj" "%BUILD%\mem.obj" ^
  "%BUILD%\platform_win32.obj" "%BUILD%\workdir.obj" "%BUILD%\state.obj" ^
  "%BUILD%\memory.obj" "%BUILD%\secrets.obj" "%BUILD%\tool.obj" "%BUILD%\mcp.obj" ^
  "%BUILD%\llm.obj" "%BUILD%\cJSON.obj" "%BUILD%\sqlite3.obj" ^
  /Fe:"%BUILD%\moyu-chat.exe" /link /SUBSYSTEM:CONSOLE winhttp.lib user32.lib gdi32.lib shell32.lib crypt32.lib ws2_32.lib
if errorlevel 1 goto :main_failed

for %%K in (1 2 3) do (
  if %%K==1 set "MCPNAME=git"
  if %%K==2 set "MCPNAME=notes"
  if %%K==3 set "MCPNAME=weather"
  clang-cl /nologo /utf-8 /c /std:c11 /O2 /D_CRT_SECURE_NO_WARNINGS /DSERVER_KIND=%%K ^
    /W3 /wd4100 /wd4244 /wd4267 ^
    /clang:-Wno-unused-function /clang:-Wno-unused-variable ^
    /I"%ROOT%\third_party\cjson" "%ROOT%\examples\mcp\reference_server.c" ^
    /Fo:"%BUILD%\mcp-!MCPNAME!.obj"
  if errorlevel 1 goto :mcp_failed
  clang-cl /nologo "%BUILD%\mcp-!MCPNAME!.obj" "%BUILD%\cJSON.obj" ^
    /Fe:"%BUILD%\moyu-mcp-!MCPNAME!.exe" ^
    /link /SUBSYSTEM:CONSOLE winhttp.lib ws2_32.lib
  if errorlevel 1 goto :mcp_failed
)

clang-cl /nologo /utf-8 /c /std:c11 /O2 /D_CRT_SECURE_NO_WARNINGS ^
  /W3 /wd4100 /wd4189 /wd4244 /wd4267 ^
  /I"%ROOT%\src" /I"%ROOT%\third_party\cjson" /I"%ROOT%\third_party\sqlite" ^
  "%ROOT%\tests\test_main.c" /Fo:"%BUILD%\test_main.obj"
if errorlevel 1 goto :tests_failed
clang-cl /nologo "%BUILD%\test_main.obj" "%BUILD%\hash.obj" "%BUILD%\log.obj" ^
  "%BUILD%\mem.obj" "%BUILD%\platform_win32.obj" "%BUILD%\workdir.obj" ^
  "%BUILD%\state.obj" "%BUILD%\memory.obj" "%BUILD%\tool.obj" "%BUILD%\mcp.obj" ^
  "%BUILD%\procedural.obj" "%BUILD%\sprite.obj" "%BUILD%\image.obj" ^
  "%BUILD%\cJSON.obj" "%BUILD%\sqlite3.obj" /Fe:"%BUILD%\moyu-tests.exe" ^
  /link /SUBSYSTEM:CONSOLE winhttp.lib user32.lib gdi32.lib shell32.lib ws2_32.lib
if errorlevel 1 goto :tests_failed

xcopy /E /I /Y /Q "%ROOT%\assets" "%BUILD%\assets" >nul
xcopy /E /I /Y /Q "%ROOT%\scripts" "%BUILD%\scripts" >nul

echo [build] OK -^> %BUILD%\moyu.exe
for %%I in ("%BUILD%\moyu.exe") do echo size: %%~zI bytes
endlocal
exit /b 0

:main_failed
echo [build] primary target failed
endlocal
exit /b 1
:mcp_failed
echo [build] MCP reference target failed
endlocal
exit /b 1
:tests_failed
echo [build] test target failed
endlocal
exit /b 1
