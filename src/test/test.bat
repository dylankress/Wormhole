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

REM --- Common compiler flags ---
set CFLAGS=/nologo /Zi /Od /W4 /MD /D_CRT_SECURE_NO_WARNINGS=1
set INCLUDES=/I "%MSQUIC_INC%" /I "%BLAKE3_ROOT%" /I ..

REM --- Main build object files ---
set BUILD=..\build
set BLAKE3_OBJS=%BUILD%\blake3.obj %BUILD%\blake3_dispatch.obj %BUILD%\blake3_portable.obj

REM --- Check that main build exists ---
if not exist "%BUILD%\manifest.obj" (
    echo ERROR: Main build not found. Run build.bat first.
    popd
    endlocal
    exit /b 1
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
if not errorlevel 1 ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
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
if not errorlevel 1 ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
set /a TOTAL+=1

:test_chunk_store
REM ===================================================================
echo.
echo --- test_chunk_store ---
cl %CFLAGS% %INCLUDES% test_chunk_store.c %BUILD%\chunk_store.obj %BUILD%\file_io.obj /Fe:test_chunk_store.exe /link ole32.lib
if errorlevel 1 (
    echo   COMPILE FAILED
    set /a FAIL_COUNT+=1
    set /a TOTAL+=1
    goto :test_transfer_state
)
test_chunk_store.exe
if not errorlevel 1 ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
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
if not errorlevel 1 ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
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
if not errorlevel 1 ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
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
    goto :summary
)
test_chunker.exe
if not errorlevel 1 ( set /a PASS_COUNT+=1 ) else ( set /a FAIL_COUNT+=1 )
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
