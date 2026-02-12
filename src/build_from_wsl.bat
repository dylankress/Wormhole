@echo off
REM build_from_wsl.bat - Wrapper that sets up VS environment and builds

REM Set up Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

REM Navigate to source directory
cd /d C:\Dev\Wormhole\src

REM Run the actual build script
call build.bat

echo.
echo Build complete! Check for errors above.
