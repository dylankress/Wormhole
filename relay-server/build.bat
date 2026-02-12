@echo off
REM
REM build.bat
REM Build script for Wormhole Relay Server (Windows)
REM by Dylan Kress
REM

echo Building Wormhole Relay Server (Windows)...

REM Create build directory
if not exist build mkdir build

REM Compiler and flags
set CC=cl.exe
set CFLAGS=/W4 /O2 /MD /nologo /D_CRT_SECURE_NO_WARNINGS /I..\deps\libsodium\include
set LDFLAGS=ws2_32.lib advapi32.lib ..\deps\libsodium\x64\Release\v143\dynamic\libsodium.lib

REM Source files
set SOURCES=main.c server.c peer_registry.c ticket_manager.c rate_limiter.c crypto.c

REM Output binary
set OUTPUT=build\relay-server.exe

REM Compile
echo Compiling...
%CC% %CFLAGS% %SOURCES% /Fe%OUTPUT% /link %LDFLAGS%

if %errorlevel% equ 0 (
    echo Build successful: %OUTPUT%
    
    REM Copy libsodium DLL to build directory
    echo Copying libsodium.dll...
    copy /Y ..\deps\libsodium\x64\Release\v143\dynamic\libsodium.dll build\ >nul
    
    dir build\relay-server.exe
    dir build\libsodium.dll
) else (
    echo Build failed!
    exit /b 1
)

REM Clean up intermediate files
del *.obj 2>nul
