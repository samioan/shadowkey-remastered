@echo off
rem M113: the shipping build, and then the folder that gets zipped.
rem
rem Differs from build.bat in four ways, all of which matter to someone who
rem downloaded this rather than built it:
rem   Release        - optimised, and /MT via CMAKE_MSVC_RUNTIME_LIBRARY, so
rem                    no Visual C++ redistributable is needed
rem   SK_DIST=ON     - builds the two shipped executables, not all 101
rem   SK_DEBUG_SUITE - off, so the debug console and overlay compile out
rem   SK_VERSION     - what the launcher prints in its corner
rem
rem Usage:  port\build_dist.bat [version]
rem Output: port\dist\  (ready to zip)  and  port\build-dist\  (objects)

setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
cd /d "%~dp0"

set SK_VER=%1
if "%SK_VER%"=="" set SK_VER=0.0.0-local

cmake -G Ninja -S . -B build-dist -DCMAKE_BUILD_TYPE=Release -DSK_DIST=ON ^
      -DSK_DEBUG_SUITE=OFF -DSK_VERSION=%SK_VER%
if errorlevel 1 exit /b 1
cmake --build build-dist
if errorlevel 1 exit /b 1

rem Assemble the layout the launcher expects: itself at the top, the game
rem in bin\. `data\` and `user\` are created by the launcher on first run,
rem not shipped -- they are where the player's own files go.
if exist dist rmdir /s /q dist
mkdir dist\bin
copy /y build-dist\Shadowkey.exe dist\ >nul
if errorlevel 1 exit /b 1
copy /y build-dist\shadowkey_port.exe dist\bin\ >nul
if errorlevel 1 exit /b 1
copy /y ..\NOTICE.md dist\ >nul
copy /y dist_readme.txt dist\README.txt >nul

echo.
echo Built %SK_VER% into port\dist\ -- zip that folder.
dir /b dist
endlocal
