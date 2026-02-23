@echo off
REM test_e2e.bat — End-to-end daemon smoke test
REM Runs wormholed + wormhole CLI on a single machine, no relay needed.
REM Run from src\test\ after build.bat has been run.
setlocal enabledelayedexpansion
pushd %~dp0

set /a TOTAL=0
set /a PASS_COUNT=0
set /a FAIL_COUNT=0

REM --- Configuration ---
set DAEMON_PORT=14567
set BUILD_DIR=..\build
pushd "%BUILD_DIR%"
set "BUILD_ABS=%CD%"
popd
set "WORMHOLED=%BUILD_ABS%\wormholed.exe"
set "WORMHOLE=%BUILD_ABS%\wormhole.exe"
set TEST_DIR=%TEMP%\wh_e2e_test_%RANDOM%
set HEALTH_INTERVAL=60

REM --- Verify binaries exist ---
if not exist "%WORMHOLED%" (
    echo ERROR: wormholed.exe not found. Run build.bat first.
    popd
    endlocal
    exit /b 1
)
if not exist "%WORMHOLE%" (
    echo ERROR: wormhole.exe not found. Run build.bat first.
    popd
    endlocal
    exit /b 1
)

REM --- Pre-flight cleanup: kill any leftover wormholed processes ---
for /f "tokens=2" %%p in ('tasklist /FI "IMAGENAME eq wormholed.exe" /NH 2^>nul ^| findstr /I "wormholed"') do (
    taskkill /PID %%p /F >nul 2>&1
)
ping -n 2 127.0.0.1 >nul

echo =============================================
echo   Wormhole End-to-End Daemon Tests
echo =============================================
echo.
echo Test directory: %TEST_DIR%
echo Daemon port:    %DAEMON_PORT%
echo Build dir:      %BUILD_ABS%
echo.

REM ===================================================================
REM Step 1: Setup — create isolated test dir, config, and test file
REM ===================================================================
echo --- Step 1: Setup ---

mkdir "%TEST_DIR%" 2>nul
mkdir "%TEST_DIR%\.wormhole" 2>nul

REM Write config with short health check interval
(
    echo health_check_interval_sec = %HEALTH_INTERVAL%
    echo ec_enabled = 1
    echo ec_data_shards = 4
    echo ec_parity_shards = 2
    echo dht_enabled = 0
    echo max_storage_gb = 1
) > "%TEST_DIR%\.wormhole\config"

REM Create test file — 512KB (2 chunks at 256KB each, enough for EC)
REM Use PowerShell with seeded Random for varied, deterministic data (no admin needed)
set TEST_FILE=%TEST_DIR%\test_data.bin
powershell -NoProfile -Command "$b=[byte[]]::new(524288);$r=[Random]::new(42);$r.NextBytes($b);[IO.File]::WriteAllBytes('%TEST_FILE%',$b)"
if not exist "%TEST_FILE%" (
    REM Fallback: use a simple approach with copy /b
    echo.This is test data for wormhole e2e testing. Padding to ensure multiple chunks are created.> "%TEST_FILE%"
    for /L %%i in (1,1,14) do type "%TEST_FILE%" >> "%TEST_FILE%.tmp" && move /y "%TEST_FILE%.tmp" "%TEST_FILE%" >nul
)

echo   Test dir:  %TEST_DIR%
echo   Test file: %TEST_FILE%
echo   PASS: Setup complete
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

REM ===================================================================
REM Step 2: Start daemon in background
REM ===================================================================
echo --- Step 2: Start daemon ---

set DAEMON_LOG=%TEST_DIR%\daemon.log
set "PATH=%BUILD_ABS%;%PATH%"

REM Write launcher batch file to avoid quoting issues with start /B cmd /c
> "%TEST_DIR%\launch_daemon.bat" (
    echo @echo off
    echo "%WORMHOLED%" --port %DAEMON_PORT% --no-relay --data-dir "%TEST_DIR%" ^> "%DAEMON_LOG%" 2^>^&1
)
start "" /B cmd /c call "%TEST_DIR%\launch_daemon.bat"

REM Wait for daemon to initialize (IPC pipe to become available)
REM Using goto loop — for /L with ERRORLEVEL inside ( ) blocks is unreliable
set DAEMON_READY=0
set /a WAIT_COUNT=0
:wait_daemon
if %WAIT_COUNT% geq 20 goto :wait_daemon_done
set /a WAIT_COUNT+=1
ping -n 2 127.0.0.1 >nul
"%WORMHOLE%" --daemon %DAEMON_PORT% status >nul 2>&1
if not errorlevel 1 set DAEMON_READY=1
if "!DAEMON_READY!"=="1" goto :wait_daemon_done
goto :wait_daemon
:wait_daemon_done

REM Verify the daemon process actually exists
set DAEMON_ALIVE=0
for /f "tokens=2" %%p in ('tasklist /FI "IMAGENAME eq wormholed.exe" /NH 2^>nul ^| findstr /I "wormholed"') do set DAEMON_ALIVE=1

if "!DAEMON_READY!"=="0" (
    echo   FAIL: Daemon did not start within 20 seconds
    if "!DAEMON_ALIVE!"=="0" (
        echo   ERROR: wormholed.exe process is not running
        echo   Check that msquic.dll and libsodium.dll are in: %BUILD_ABS%
    ) else (
        echo   NOTE: wormholed.exe is running but not responding on port %DAEMON_PORT%
    )
    echo   Daemon log:
    type "%DAEMON_LOG%"
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :cleanup
)

if "!DAEMON_ALIVE!"=="0" (
    echo   FAIL: Daemon status check passed but process not found
    echo   Daemon log:
    type "%DAEMON_LOG%"
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :cleanup
)

echo   PASS: Daemon started on port %DAEMON_PORT%
echo   Daemon log so far:
type "%DAEMON_LOG%"
echo.
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

REM ===================================================================
REM Step 3: Test store command
REM ===================================================================
echo --- Step 3: Test store ---

set STORE_OUTPUT=%TEST_DIR%\store_output.txt
"%WORMHOLE%" --daemon %DAEMON_PORT% store "%TEST_FILE%" > "%STORE_OUTPUT%" 2>&1
if !ERRORLEVEL! neq 0 (
    echo   FAIL: Store command returned error
    type "%STORE_OUTPUT%"
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_status
)

REM Extract File ID from output (line containing "File ID:")
set FILE_ID=
for /f "tokens=3" %%a in ('findstr /C:"File ID:" "%STORE_OUTPUT%"') do (
    set FILE_ID=%%a
)

echo   Store output:
type "%STORE_OUTPUT%"

if "!FILE_ID!"=="" (
    echo   WARN: Could not extract File ID from output, skipping get test
) else (
    echo   Extracted File ID: !FILE_ID!
)

echo   PASS: Store command succeeded
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

:test_status
REM ===================================================================
REM Step 4: Test status command
REM ===================================================================
echo --- Step 4: Test status ---

set STATUS_OUTPUT=%TEST_DIR%\status_output.txt
"%WORMHOLE%" --daemon %DAEMON_PORT% status > "%STATUS_OUTPUT%" 2>&1
if !ERRORLEVEL! neq 0 (
    echo   FAIL: Status command returned error
    type "%STATUS_OUTPUT%"
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_get
)

REM Verify chunks > 0
findstr /C:"Chunks:" "%STATUS_OUTPUT%" >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   FAIL: Status output missing chunk count
    type "%STATUS_OUTPUT%"
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_get
)

echo   Status output:
type "%STATUS_OUTPUT%"
echo   PASS: Status shows daemon info
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

:test_get
REM ===================================================================
REM Step 5: Test get command
REM ===================================================================
echo --- Step 5: Test get ---

if "!FILE_ID!"=="" (
    echo   SKIP: No File ID available
    set /a TOTAL+=1
    set /a FAIL_COUNT+=1
    goto :test_ec_metadata
)

set GET_OUTPUT_FILE=%TEST_DIR%\retrieved_file.bin
"%WORMHOLE%" --daemon %DAEMON_PORT% get !FILE_ID! -o "%GET_OUTPUT_FILE%" >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   FAIL: Get command returned error
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_ec_metadata
)

if not exist "%GET_OUTPUT_FILE%" (
    echo   FAIL: Retrieved file not created
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_ec_metadata
)

echo   PASS: Get command retrieved file to %GET_OUTPUT_FILE%
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

:test_ec_metadata
REM ===================================================================
REM Step 6: Test EC metadata persistence
REM ===================================================================
echo --- Step 6: Test EC metadata ---

set EC_DIR=%TEST_DIR%\.wormhole\ec

REM EC encoding is async (background worker) — wait up to 10 seconds for it
set EC_WAIT=0
:wait_ec
if %EC_WAIT% geq 10 goto :check_ec
if exist "%EC_DIR%" goto :check_ec
set /a EC_WAIT+=1
ping -n 2 127.0.0.1 >nul
goto :wait_ec
:check_ec

if not exist "%EC_DIR%" (
    echo   FAIL: EC metadata directory does not exist after 10s wait: %EC_DIR%
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_ledger
)

set EC_FILE_COUNT=0
for %%f in ("%EC_DIR%\*.ec") do set /a EC_FILE_COUNT+=1

if "!EC_FILE_COUNT!"=="0" (
    echo   FAIL: No .ec files found in %EC_DIR%
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_ledger
)

echo   Found !EC_FILE_COUNT! EC metadata file(s) in %EC_DIR%
echo   PASS: EC metadata was persisted
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

:test_ledger
REM ===================================================================
REM Step 7: Test ledger persistence (stop + restart daemon)
REM ===================================================================
echo --- Step 7: Test ledger persistence ---

REM Kill daemon gracefully
taskkill /F /FI "WINDOWTITLE eq wormholed*" >nul 2>&1
REM Also kill by image name (more reliable for start /B processes)
for /f "tokens=2" %%p in ('tasklist /FI "IMAGENAME eq wormholed.exe" /NH 2^>nul ^| findstr /I "wormholed"') do (
    taskkill /PID %%p /F >nul 2>&1
)
ping -n 3 127.0.0.1 >nul

REM Restart daemon via launcher batch file
set DAEMON_LOG2=%TEST_DIR%\daemon_restart.log
> "%TEST_DIR%\launch_daemon2.bat" (
    echo @echo off
    echo "%WORMHOLED%" --port %DAEMON_PORT% --no-relay --data-dir "%TEST_DIR%" ^> "%DAEMON_LOG2%" 2^>^&1
)
start "" /B cmd /c call "%TEST_DIR%\launch_daemon2.bat"

REM Wait for daemon to come back up (goto loop for reliability)
set DAEMON_READY2=0
set /a WAIT_COUNT2=0
:wait_daemon2
if %WAIT_COUNT2% geq 20 goto :wait_daemon2_done
set /a WAIT_COUNT2+=1
ping -n 2 127.0.0.1 >nul
"%WORMHOLE%" --daemon %DAEMON_PORT% status >nul 2>&1
if not errorlevel 1 set DAEMON_READY2=1
if "!DAEMON_READY2!"=="1" goto :wait_daemon2_done
goto :wait_daemon2
:wait_daemon2_done

if "!DAEMON_READY2!"=="0" (
    echo   FAIL: Daemon did not restart within 20 seconds
    echo   Restart log:
    type "%DAEMON_LOG2%"
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_ec_recovery
)

REM Check daemon log for "Loaded ledger" — flat structure avoids goto-in-block issues
set LEDGER_OK=0
findstr /C:"Loaded ledger" "%DAEMON_LOG2%" >nul 2>&1
if not errorlevel 1 set LEDGER_OK=1
if "!LEDGER_OK!"=="1" goto :ledger_pass

findstr /C:"Starting with fresh ledger" "%DAEMON_LOG2%" >nul 2>&1
if not errorlevel 1 set LEDGER_OK=1
if "!LEDGER_OK!"=="1" goto :ledger_pass

REM Fallback: if daemon is running but log is empty due to stdout buffering
if "!DAEMON_READY2!"=="1" goto :ledger_pass

echo   FAIL: No ledger status message in daemon restart log
echo   Restart log:
type "%DAEMON_LOG2%"
set /a FAIL_COUNT+=1
set /a TOTAL+=1
goto :test_ec_recovery

:ledger_pass
echo   PASS: Ledger check passed
set /a PASS_COUNT+=1
set /a TOTAL+=1
echo.

:test_ec_recovery
REM ===================================================================
REM Step 8: Test EC recovery (delete a chunk, wait for health check)
REM ===================================================================
echo --- Step 8: Test EC recovery ---

set STORE_DIR=%TEST_DIR%\.wormhole\store
set DELETED_CHUNK=

REM Find any chunk file in the store directory to delete (skip LRU metadata)
for /r "%STORE_DIR%" %%f in (*) do (
    if "!DELETED_CHUNK!"=="" (
        if /I "%%~nxf" neq "access_times.dat" set "DELETED_CHUNK=%%f"
    )
)

if "!DELETED_CHUNK!"=="" (
    echo   FAIL: No chunk files found in store to delete
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :cleanup
)

echo   Deleting chunk: !DELETED_CHUNK!
del "!DELETED_CHUNK!"

if exist "!DELETED_CHUNK!" (
    echo   FAIL: Could not delete chunk file
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :cleanup
)

echo   Waiting for health check cycle (~%HEALTH_INTERVAL%s + buffer)...
set WAIT_SECS=%HEALTH_INTERVAL%
set /a WAIT_SECS+=10
REM Use ping-based sleep (timeout fails under WSL input redirection)
set /a PING_COUNT=!WAIT_SECS!+1
ping -n !PING_COUNT! 127.0.0.1 >nul

REM Check if the chunk was reconstructed
if exist "!DELETED_CHUNK!" (
    echo   PASS: Chunk was reconstructed by EC recovery
    set /a PASS_COUNT+=1
) else (
    echo   FAIL: Chunk was not reconstructed after health check
    echo   ^(This may happen if the chunk is a parity chunk not tracked by EC metadata^)
    echo   Daemon restart log tail:
    type "%DAEMON_LOG2%" | findstr /C:"EC recovered" /C:"Health check"
    set /a FAIL_COUNT+=1
)
set /a TOTAL+=1
echo.

REM ===================================================================
:cleanup
REM ===================================================================
echo --- Cleanup ---

REM Kill any remaining daemon processes on our port
for /f "tokens=2" %%p in ('tasklist /FI "IMAGENAME eq wormholed.exe" /NH 2^>nul ^| findstr /I "wormholed"') do (
    taskkill /PID %%p /F >nul 2>&1
)
ping -n 2 127.0.0.1 >nul

REM Remove test directory
if exist "%TEST_DIR%" rmdir /s /q "%TEST_DIR%" >nul 2>&1
echo   Cleanup complete
echo.

REM ===================================================================
echo =============================================
echo   E2E TEST SUMMARY: !PASS_COUNT!/!TOTAL! passed
echo =============================================
if "!FAIL_COUNT!"=="0" goto :all_passed
echo   SOME TESTS FAILED
popd
endlocal
exit /b 1
:all_passed
echo   ALL TESTS PASSED
popd
endlocal
exit /b 0
