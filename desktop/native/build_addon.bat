@echo off
setlocal

set VSBASE=C:\Program Files\Microsoft Visual Studio\18\Community
set VCVARS="%VSBASE%\VC\Auxiliary\Build\vcvars64.bat"
set SDK_VER=10.0.26100.0
set SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\%SDK_VER%\um
set SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\%SDK_VER%\um\x64

call %VCVARS%

:: Get node headers path
set NAPI_INC=c:\Users\ajibe\peercam\desktop\node_modules\node-addon-api
set NODE_INC=C:\Users\ajibe\AppData\Local\node-gyp\Cache\34.5.8\include\node
set PEERCAM_LIB=%~dp0peercam_node.lib
set OUT=build\Release

if not exist %OUT% mkdir %OUT%

echo Building vcam.node...

cl /LD /O2 /EHsc /MT /W0 /DWIN32 /DNDEBUG /DNAPI_DISABLE_CPP_EXCEPTIONS /DBUILDING_NODE_EXTENSION ^
   /I"%NAPI_INC%" ^
   /I"%NODE_INC%" ^
   vcam.cc vcam_win.cc ^
   "%PEERCAM_LIB%" ^
   kernel32.lib advapi32.lib ^
   /Fe:%OUT%\vcam.node ^
   /link /SUBSYSTEM:WINDOWS /DLL

if %ERRORLEVEL% neq 0 ( echo vcam.node build failed. & exit /b 1 )

echo.
echo SUCCESS: %OUT%\vcam.node built.
