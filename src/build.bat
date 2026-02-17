REM build.bat - Wormhole with Relay Integration + Content-Addressed Chunking + P2P Storage
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

REM --- Blake3 paths (portable-only: no SIMD assembly)
set BLAKE3_ROOT=..\..\deps\blake3

REM --- Reed-Solomon paths (erasure coding)
set RS_ROOT=..\..\deps\reed_solomon

REM --- DHT sources
set DHT_SOURCES=..\dht\routing_table.c ..\dht\dht_node.c ..\dht\dht_store.c ..\dht\dht_lookup.c

REM --- Relay client sources
set RELAY_SOURCES=..\relay\peer_id.c ..\relay\relay_client.c ..\relay\discovery.c ..\relay\ticket.c

REM === Build 1: wormhole.exe (CLI) ===
cl /Zi /Od /W4 /MD ^
	/I "%MSQUIC_INC%" ^
	/I "%LIBSODIUM_INC%" ^
	/I "%BLAKE3_ROOT%" ^
	/I "%RS_ROOT%" ^
	/I .. ^
	/DBLAKE3_NO_SSE2 /DBLAKE3_NO_SSE41 /DBLAKE3_NO_AVX2 /DBLAKE3_NO_AVX512 /DQUIC_API_ENABLE_PREVIEW_FEATURES ^
	..\wormhole.c ^
	..\stream.c ^
	..\file_io.c ^
	..\crypto.c ^
	..\manifest.c ^
	..\chunker.c ^
	..\chunk_store.c ^
	..\transfer_state.c ^
	..\config.c ^
	..\ipc.c ^
	..\erasure.c ^
	..\proof.c ^
	..\incentives.c ^
	..\health.c ^
	..\relay_forwarder.c ^
	"%BLAKE3_ROOT%\blake3.c" ^
	"%BLAKE3_ROOT%\blake3_dispatch.c" ^
	"%BLAKE3_ROOT%\blake3_portable.c" ^
	"%RS_ROOT%\rs.c" ^
	%DHT_SOURCES% ^
	%RELAY_SOURCES% ^
	"%MSQUIC_LIB%" ^
	"%LIBSODIUM_LIB%" ^
	ws2_32.lib bcrypt.lib advapi32.lib iphlpapi.lib ole32.lib ^
	/Fe:wormhole.exe

if %ERRORLEVEL% NEQ 0 (
	echo ERROR: wormhole.exe build failed
	goto :done
)

REM === Build 2: wormholed.exe (Daemon) ===
cl /Zi /Od /W4 /MD ^
	/I "%MSQUIC_INC%" ^
	/I "%LIBSODIUM_INC%" ^
	/I "%BLAKE3_ROOT%" ^
	/I "%RS_ROOT%" ^
	/I .. ^
	/DBLAKE3_NO_SSE2 /DBLAKE3_NO_SSE41 /DBLAKE3_NO_AVX2 /DBLAKE3_NO_AVX512 /DQUIC_API_ENABLE_PREVIEW_FEATURES ^
	..\wormholed.c ^
	..\stream.c ^
	..\file_io.c ^
	..\crypto.c ^
	..\manifest.c ^
	..\chunker.c ^
	..\chunk_store.c ^
	..\transfer_state.c ^
	..\config.c ^
	..\ipc.c ^
	..\erasure.c ^
	..\proof.c ^
	..\incentives.c ^
	..\health.c ^
	..\relay_forwarder.c ^
	"%BLAKE3_ROOT%\blake3.c" ^
	"%BLAKE3_ROOT%\blake3_dispatch.c" ^
	"%BLAKE3_ROOT%\blake3_portable.c" ^
	"%RS_ROOT%\rs.c" ^
	%DHT_SOURCES% ^
	%RELAY_SOURCES% ^
	"%MSQUIC_LIB%" ^
	"%LIBSODIUM_LIB%" ^
	ws2_32.lib bcrypt.lib advapi32.lib iphlpapi.lib ole32.lib ^
	/Fe:wormholed.exe

if %ERRORLEVEL% NEQ 0 (
	echo ERROR: wormholed.exe build failed
	goto :done
)

echo.
echo Build successful: wormhole.exe + wormholed.exe

:done

REM --- Copy runtime DLLs next to the exe so it launches
copy /Y "%MSQUIC_DLL%" .
copy /Y "%LIBSODIUM_DLL%" .

popd
popd
endlocal
