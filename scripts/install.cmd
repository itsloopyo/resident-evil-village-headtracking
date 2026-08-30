@echo off
:: ============================================
:: Resident Evil Village - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-reframework.cmd.

:: --- CONFIG BLOCK ---
set "GAME_ID=resident-evil-village"
set "MOD_DISPLAY_NAME=RE8 Head Tracking"
set "MOD_DLLS=RE8HeadTracking.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=RE8HeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=REFramework"
set "REFRAMEWORK_VENDOR_ZIP_NAME=REFramework.zip"
set "MOD_CONTROLS=Controls (nav-cluster keys, or Ctrl+Shift chords for keyboards without a nav cluster):&echo   End  / Ctrl+Shift+Y - Toggle head tracking&echo   PgUp / Ctrl+Shift+G - Toggle positional tracking&echo   PgDn / Ctrl+Shift+H - Toggle world/camera-local yaw&echo   F9                  - Toggle world-anchored GUI markers"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-reframework.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-reframework.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-reframework.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%