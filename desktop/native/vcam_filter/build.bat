@echo off
setlocal

set VSBASE=C:\Program Files\Microsoft Visual Studio\18\Community
set VCVARS="%VSBASE%\VC\Auxiliary\Build\vcvars64.bat"
set SDK_VER=10.0.26100.0
set SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\%SDK_VER%\um
set SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\%SDK_VER%\um\x64
set BASECLASSES=..\baseclasses
set OUTDIR=..\baseclasses\lib

if not exist %VCVARS% (
    echo ERROR: VS not found. Install Desktop development with C++ workload.
    exit /b 1
)
call %VCVARS%

:: Build BaseClasses static library
echo Building DirectShow BaseClasses...
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /c /O2 /W0 /EHsc /MT /DWIN32 /D_WINDOWS /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"%SDK_INC%" ^
   /I"%BASECLASSES%" ^
   /Fo"%OUTDIR%\\" ^
   %BASECLASSES%\amextra.cpp ^
   %BASECLASSES%\amfilter.cpp ^
   %BASECLASSES%\amvideo.cpp ^
   %BASECLASSES%\arithutil.cpp ^
   %BASECLASSES%\combase.cpp ^
   %BASECLASSES%\cprop.cpp ^
   %BASECLASSES%\ctlutil.cpp ^
   %BASECLASSES%\ddmm.cpp ^
   %BASECLASSES%\dllentry.cpp ^
   %BASECLASSES%\dllsetup.cpp ^
   %BASECLASSES%\mtype.cpp ^
   %BASECLASSES%\outputq.cpp ^
   %BASECLASSES%\perflog.cpp ^
   %BASECLASSES%\pstream.cpp ^
   %BASECLASSES%\pullpin.cpp ^
   %BASECLASSES%\refclock.cpp ^
   %BASECLASSES%\renbase.cpp ^
   %BASECLASSES%\schedule.cpp ^
   %BASECLASSES%\seekpt.cpp ^
   %BASECLASSES%\source.cpp ^
   %BASECLASSES%\strmctl.cpp ^
   %BASECLASSES%\sysclock.cpp ^
   %BASECLASSES%\transfrm.cpp ^
   %BASECLASSES%\transip.cpp ^
   %BASECLASSES%\videoctl.cpp ^
   %BASECLASSES%\vtrans.cpp ^
   %BASECLASSES%\winctrl.cpp ^
   %BASECLASSES%\winutil.cpp ^
   %BASECLASSES%\wxdebug.cpp ^
   %BASECLASSES%\wxlist.cpp ^
   %BASECLASSES%\wxutil.cpp

if %ERRORLEVEL% neq 0 ( echo BaseClasses compile failed. & exit /b 1 )

lib /OUT:"%OUTDIR%\strmbase.lib" "%OUTDIR%\*.obj"
if %ERRORLEVEL% neq 0 ( echo BaseClasses lib failed. & exit /b 1 )
echo BaseClasses built: %OUTDIR%\strmbase.lib

:: Build PeerCamVCam.dll
echo Building PeerCamVCam.dll...
cl /LD /O2 /EHsc /MT /W3 /DWIN32 /D_WINDOWS /DNDEBUG /DUNICODE /D_UNICODE ^
   /I"%SDK_INC%" ^
   /I"%BASECLASSES%" ^
   /I".." ^
   PeerCamVCam.cpp ^
   "%OUTDIR%\strmbase.lib" ^
   "%SDK_LIB%\ole32.lib" ^
   "%SDK_LIB%\oleaut32.lib" ^
   "%SDK_LIB%\strmiids.lib" ^
   "%SDK_LIB%\uuid.lib" ^
   kernel32.lib user32.lib advapi32.lib winmm.lib ^
   /Fe:PeerCamVCam.dll ^
   /link /DEF:PeerCamVCam.def /SUBSYSTEM:WINDOWS

if %ERRORLEVEL% neq 0 ( echo DLL build failed. & exit /b 1 )

echo.
echo SUCCESS: PeerCamVCam.dll built.
echo To test: regsvr32 PeerCamVCam.dll
echo To remove: regsvr32 /u PeerCamVCam.dll
