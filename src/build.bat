REM build.bat - Wormhole with Relay Integration
@echo off
setlocal
pushd %~dp0

if not exist build mkdir build
pushd build

REM --- MsQuic paths (adjust if you switch config/Release later)
set MSQUIC_ROOT=..\..\msquic
set MSQUIC_INC=%MSQUIC_ROOT%\src\inc
set MSQUIC_LIB=%MSQUIC_ROOT%\artifacts\bin\windows\x64_Debug_schannel\msquic.lib
set MSQUIC_DLL=%MSQUIC_ROOT%\artifacts\bin\windows\x64_Debug_schannel\msquic.dll

REM --- libsodium paths
set LIBSODIUM_ROOT=..\..\deps\libsodium
set LIBSODIUM_INC=%LIBSODIUM_ROOT%\include
set LIBSODIUM_LIB=%LIBSODIUM_ROOT%\x64\Release\v143\dynamic\libsodium.lib
set LIBSODIUM_DLL=%LIBSODIUM_ROOT%\x64\Release\v143\dynamic\libsodium.dll

REM --- Relay client sources
set RELAY_SOURCES=..\relay\peer_id.c ..\relay\relay_client.c ..\relay\discovery.c ..\relay\ticket.c ..\relay\connection_manager.c

REM --- Compile + link
cl /Zi /Od /W4 /MD ^
	/I "%MSQUIC_INC%" ^
	/I "%LIBSODIUM_INC%" ^
	/I .. ^
	..\wormhole.c ^
	..\connection.c ^
	..\stream.c ^
	..\file_io.c ^
	..\crypto.c ^
	%RELAY_SOURCES% ^
	"%MSQUIC_LIB%" ^
	"%LIBSODIUM_LIB%" ^
	ws2_32.lib bcrypt.lib advapi32.lib iphlpapi.lib ole32.lib ^
	/Fe:wormhole.exe

REM --- Copy runtime DLLs next to the exe so it launches
copy /Y "%MSQUIC_DLL%" .
copy /Y "%LIBSODIUM_DLL%" .

popd
popd
endlocal
