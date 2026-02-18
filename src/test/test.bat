@echo off
REM test.bat — Build and run Wormhole unit tests
REM Run from a Visual Studio Developer Command Prompt
setlocal enabledelayedexpansion
pushd %~dp0

REM --- Check for compiler ---
where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found. Run from a Developer Command Prompt.
    popd
    endlocal
    exit /b 1
)

REM --- Dependency paths (relative to src\test\) ---
set MSQUIC_INC=..\..\msquic\src\inc
set BLAKE3_ROOT=..\..\deps\blake3
set LIBSODIUM_ROOT=..\..\deps\libsodium
set LIBSODIUM_INC=%LIBSODIUM_ROOT%\include
set LIBSODIUM_LIB=%LIBSODIUM_ROOT%\x64\Release\v143\dynamic\libsodium.lib

REM --- Common compiler flags ---
set CFLAGS=/nologo /Zi /Od /W4 /MD /D_CRT_SECURE_NO_WARNINGS=1
set INCLUDES=/I "%MSQUIC_INC%" /I "%BLAKE3_ROOT%" /I ..

REM --- Main build object files ---
set BUILD=..\build
set BLAKE3_OBJS=%BUILD%\blake3.obj %BUILD%\blake3_dispatch.obj %BUILD%\blake3_portable.obj
set RS_ROOT=..\..\deps\reed_solomon

REM --- Check that main build exists ---
if not exist "%BUILD%\manifest.obj" (
    echo ERROR: Main build not found. Run build.bat first.
    popd
    endlocal
    exit /b 1
)

REM --- Copy runtime DLLs needed by some tests ---
if exist "%LIBSODIUM_ROOT%\x64\Release\v143\dynamic\libsodium.dll" (
    copy /Y "%LIBSODIUM_ROOT%\x64\Release\v143\dynamic\libsodium.dll" . >nul
)

REM --- Clean previous test artifacts ---
del /q *.obj *.pdb *.ilk *.exe >nul 2>&1

set /a TOTAL=0
set /a PASS_COUNT=0
set /a FAIL_COUNT=0

REM ===================================================================
echo.
echo --- test_wire_format ---
cl %CFLAGS% %INCLUDES% test_wire_format.c /Fe:test_wire_format.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_manifest
)
test_wire_format.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_manifest
REM ===================================================================
echo.
echo --- test_manifest ---
cl %CFLAGS% %INCLUDES% test_manifest.c %BUILD%\manifest.obj %BLAKE3_OBJS% /Fe:test_manifest.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_chunk_store
)
test_manifest.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_chunk_store
REM ===================================================================
echo.
echo --- test_chunk_store ---
cl %CFLAGS% %INCLUDES% test_chunk_store.c %BUILD%\chunk_store.obj %BUILD%\file_io.obj %BUILD%\proof.obj %BLAKE3_OBJS% "%LIBSODIUM_LIB%" /Fe:test_chunk_store.exe /link ole32.lib
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_transfer_state
)
test_chunk_store.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_transfer_state
REM ===================================================================
echo.
echo --- test_transfer_state ---
cl %CFLAGS% %INCLUDES% test_transfer_state.c %BUILD%\transfer_state.obj %BUILD%\file_io.obj /Fe:test_transfer_state.exe /link ole32.lib
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_config
)
test_transfer_state.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_config
REM ===================================================================
echo.
echo --- test_config ---
cl %CFLAGS% %INCLUDES% test_config.c %BUILD%\config.obj /Fe:test_config.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_chunker
)
test_config.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_chunker
REM ===================================================================
echo.
echo --- test_chunker ---
cl %CFLAGS% %INCLUDES% test_chunker.c %BUILD%\chunker.obj %BUILD%\manifest.obj %BUILD%\file_io.obj %BLAKE3_OBJS% /Fe:test_chunker.exe /link ole32.lib
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_reed_solomon
)
test_chunker.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_reed_solomon
REM ===================================================================
echo.
echo --- test_reed_solomon ---
cl %CFLAGS% %INCLUDES% /I "%RS_ROOT%" test_reed_solomon.c %RS_ROOT%\rs.c /Fe:test_reed_solomon.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_erasure
)
test_reed_solomon.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_erasure
REM ===================================================================
echo.
echo --- test_erasure ---
cl %CFLAGS% %INCLUDES% /I "%RS_ROOT%" test_erasure.c %BUILD%\erasure.obj %RS_ROOT%\rs.c %BUILD%\chunk_store.obj %BUILD%\manifest.obj %BUILD%\file_io.obj %BUILD%\proof.obj %BLAKE3_OBJS% "%LIBSODIUM_LIB%" /Fe:test_erasure.exe /link ole32.lib
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_routing_table
)
test_erasure.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_routing_table
REM ===================================================================
echo.
echo --- test_routing_table ---
cl %CFLAGS% %INCLUDES% test_routing_table.c %BUILD%\routing_table.obj /Fe:test_routing_table.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_dht_protocol
)
test_routing_table.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_dht_protocol
REM ===================================================================
echo.
echo --- test_dht_protocol ---
cl %CFLAGS% %INCLUDES% /I "%LIBSODIUM_INC%" test_dht_protocol.c %BUILD%\peer_id.obj "%LIBSODIUM_LIB%" /Fe:test_dht_protocol.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_dht_store
)
test_dht_protocol.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_dht_store
REM ===================================================================
echo.
echo --- test_dht_store ---
cl %CFLAGS% %INCLUDES% test_dht_store.c %BUILD%\dht_store.obj /Fe:test_dht_store.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_dht_lookup
)
test_dht_store.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_dht_lookup
REM ===================================================================
echo.
echo --- test_dht_lookup ---
cl %CFLAGS% %INCLUDES% test_dht_lookup.c %BUILD%\dht_lookup.obj %BUILD%\routing_table.obj /Fe:test_dht_lookup.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_proof
)
test_dht_lookup.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_proof
REM ===================================================================
echo.
echo --- test_proof ---
cl %CFLAGS% %INCLUDES% /I "%LIBSODIUM_INC%" test_proof.c %BUILD%\proof.obj %BLAKE3_OBJS% "%LIBSODIUM_LIB%" /Fe:test_proof.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_incentives
)
test_proof.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_incentives
REM ===================================================================
echo.
echo --- test_incentives ---
cl %CFLAGS% %INCLUDES% test_incentives.c %BUILD%\incentives.obj /Fe:test_incentives.exe
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_health
)
test_incentives.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_health
REM ===================================================================
echo.
echo --- test_health ---
cl %CFLAGS% %INCLUDES% /I "%RS_ROOT%" test_health.c %BUILD%\health.obj %BUILD%\erasure.obj %RS_ROOT%\rs.c %BUILD%\chunk_store.obj %BUILD%\manifest.obj %BUILD%\file_io.obj %BUILD%\proof.obj %BLAKE3_OBJS% "%LIBSODIUM_LIB%" /Fe:test_health.exe /link ole32.lib
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :summary
)
test_health.exe
if "!ERRORLEVEL!"=="0" ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:summary
REM ===================================================================
echo.
echo =============================================
echo   TEST SUMMARY: !PASS_COUNT!/!TOTAL! passed
echo =============================================
if !FAIL_COUNT! gtr 0 (
    echo   SOME TESTS FAILED
    popd
    endlocal
    exit /b 1
)
echo   ALL TESTS PASSED
popd
endlocal
exit /b 0
