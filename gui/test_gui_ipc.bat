@echo off
REM test_gui_ipc.bat — Headless GUI IPC test (Windows, single-node, 15 tests)
REM Starts 1 daemon, runs tests 1-14 + 22. Tests 15-21 are SKIPPED (need multi-node).
REM For full 22-test run, use Docker: cd docker && ./test_gui_ipc.sh
REM
REM Requires: src\build.bat already run, gui\build_windows built
REM Run from: gui\
setlocal enabledelayedexpansion
pushd %~dp0

set DAEMON=..\src\build\wormholed.exe
set WORMHOLE=..\src\build\wormhole.exe
set TEST_BIN=build_windows\Release\test_gui_ipc.exe
set EXIT_CODE=1

echo ===========================================
echo   GUI IPC Test (Windows, Single-Node)
echo ===========================================
echo.
echo Tests 15-21 (replication/transfer) require Docker multi-node.
echo Run: cd docker ^&^& ./test_gui_ipc.sh
echo.

REM --- Verify binaries exist ---
if not exist "%DAEMON%" (
    echo ERROR: Daemon not found at %DAEMON%
    echo        Build it first: cd src ^&^& build.bat
    popd
    endlocal
    exit /b 1
)

if not exist "%TEST_BIN%" (
    echo ERROR: Test binary not found at %TEST_BIN%
    echo        Build it first: cd gui ^&^& cmake -B build_windows ^&^& cmake --build build_windows --config Release
    popd
    endlocal
    exit /b 1
)

REM Resolve absolute paths
pushd "..\src\build"
set "BUILD_ABS=%CD%"
popd
set "DAEMON=%BUILD_ABS%\wormholed.exe"
set "WORMHOLE=%BUILD_ABS%\wormhole.exe"

REM --- Pre-flight cleanup: kill any leftover wormholed processes ---
for /f "tokens=2" %%p in ('tasklist /FI "IMAGENAME eq wormholed.exe" /NH 2^>nul ^| findstr /I "wormholed"') do (
    taskkill /PID %%p /F >nul 2>&1
)
ping -n 2 127.0.0.1 >nul

REM --- Create temp directory ---
set TEST_BASE=%TEMP%\wh_gui_test_%RANDOM%
mkdir "%TEST_BASE%" 2>nul
echo Test dir: %TEST_BASE%

REM --- Create daemon home with config ---
set HOME1=%TEST_BASE%\home1
mkdir "%HOME1%\.wormhole" 2>nul

(
    echo relay_host=wormholerelay.com
    echo relay_port=443
    echo dht_enabled=1
    echo dht_port=14568
    echo ec_enabled=1
    echo max_storage_gb=1
    echo replication_target=3
    echo min_storage_ratio=0
    echo health_check_interval_sec=60
) > "%HOME1%\.wormhole\config"

REM --- Generate 1 MB test file via PowerShell ---
set TEST_FILE=%TEST_BASE%\test_1mb.bin
powershell -NoProfile -Command "$b=[byte[]]::new(1048576);$r=[Random]::new(42);$r.NextBytes($b);[IO.File]::WriteAllBytes('%TEST_FILE%',$b)"
if not exist "%TEST_FILE%" (
    echo ERROR: Failed to create test file
    goto :cleanup
)
echo Test file: %TEST_FILE% (1 MB)
echo.

REM --- Start daemon (port 4567) ---
echo Starting daemon (QUIC:4567, DHT:14568)...

set "PATH=%BUILD_ABS%;%PATH%"
set DAEMON1_LOG=%TEST_BASE%\node1.log

> "%TEST_BASE%\launch_daemon1.bat" (
    echo @echo off
    echo set "USERPROFILE=%HOME1%"
    echo set "HOME=%HOME1%"
    echo "%DAEMON%" --port 4567 ^> "%DAEMON1_LOG%" 2^>^&1
)
start "" /B cmd /c call "%TEST_BASE%\launch_daemon1.bat"

REM Wait for daemon
set /a WAIT_COUNT=0
:wait_daemon1
if !WAIT_COUNT! geq 30 goto :wait_daemon1_done
set /a WAIT_COUNT+=1
ping -n 2 127.0.0.1 >nul
set "USERPROFILE=%HOME1%"
set "HOME=%HOME1%"
"%WORMHOLE%" --daemon 4567 status >nul 2>&1
if not errorlevel 1 goto :wait_daemon1_done
goto :wait_daemon1
:wait_daemon1_done

set "USERPROFILE=%HOME1%"
set "HOME=%HOME1%"
"%WORMHOLE%" --daemon 4567 status >nul 2>&1
if errorlevel 1 (
    echo ERROR: Daemon failed to start within 60 seconds
    echo --- node1.log ---
    type "%DAEMON1_LOG%" 2>nul
    goto :cleanup
)
echo Daemon running

REM --- Run the test ---
echo.
echo --- Running test binary ---
echo.

set "USERPROFILE=%HOME1%"
set "HOME=%HOME1%"
"%~dp0%TEST_BIN%" --test-file "%TEST_FILE%" --socket wormhole_4567
set EXIT_CODE=!ERRORLEVEL!

echo.
if "!EXIT_CODE!"=="0" (
    echo === ALL TESTS PASSED ===
) else (
    echo === SOME TESTS FAILED (exit code !EXIT_CODE!) ===
    echo.
    echo --- Diagnostic Logs ---
    if exist "%DAEMON1_LOG%" (
        echo --- node1.log ^(last 20 lines^) ---
        powershell -NoProfile -Command "Get-Content '%DAEMON1_LOG%' -Tail 20" 2>nul
    )
)

REM ===================================================================
:cleanup
REM ===================================================================
echo.
echo Cleaning up...

REM Kill all wormholed processes we started
for /f "tokens=2" %%p in ('tasklist /FI "IMAGENAME eq wormholed.exe" /NH 2^>nul ^| findstr /I "wormholed"') do (
    taskkill /PID %%p /F >nul 2>&1
)
ping -n 2 127.0.0.1 >nul

REM Remove test directory
if exist "%TEST_BASE%" rmdir /s /q "%TEST_BASE%" >nul 2>&1
echo Cleanup complete

popd
endlocal
exit /b %EXIT_CODE%
