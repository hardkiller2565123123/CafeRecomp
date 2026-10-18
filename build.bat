@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" || exit /b 1
set "CAFE_CMAKE="
for %%I in (cmake.exe) do set "CAFE_CMAKE=%%~$PATH:I"
if defined CAFE_CMAKE goto found
if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CAFE_CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
if defined CAFE_CMAKE goto found
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CAFE_CMAKE=%%I"
if not defined CAFE_CMAKE (
  echo CMake not found. Install Visual Studio Desktop development with C++ and CMake.
  popd
  exit /b 1
)
:found
set "CAFE_MODE=%~1"
if not defined CAFE_MODE set "CAFE_MODE=build"
set "CAFE_CONFIG=Release"
set "CAFE_PRESET=win-amd64-release"
if /i "%~2"=="debug" (
  set "CAFE_CONFIG=Debug"
  set "CAFE_PRESET=win-amd64-debug"
)
if not "%~2"=="" if /i not "%~2"=="debug" if /i not "%~2"=="release" goto usage_error
if /i "%CAFE_MODE%"=="help" goto help
if /i "%CAFE_MODE%"=="generate" goto generate
if /i "%CAFE_MODE%"=="test" goto test
if /i "%CAFE_MODE%"=="package" goto package
if /i "%CAFE_MODE%"=="configure" goto configure
if /i not "%CAFE_MODE%"=="build" goto usage_error
if not exist "recomp\generated\generated.h" (
  call :generate_sources
  if errorlevel 1 goto failed
)
:configure
"%CAFE_CMAKE%" --preset "%CAFE_PRESET%"
if errorlevel 1 goto failed
if /i "%CAFE_MODE%"=="configure" goto success
"%CAFE_CMAKE%" --build --preset "%CAFE_PRESET%" --parallel 8
if errorlevel 1 goto failed
echo Built build\bin\botw_recomp.exe and its runtime DLLs.
goto success
:generate
call :generate_sources
if errorlevel 1 goto failed
goto success
:generate_sources
"%CAFE_CMAKE%" -S . -B build\codegen -A x64 -DCAFERECOMP_COMPILER_ONLY=ON
if errorlevel 1 exit /b 1
"%CAFE_CMAKE%" --build build\codegen --config Release --target cafe_codegen --parallel 8
if errorlevel 1 exit /b 1
"%CAFE_CMAKE%" -DCAFE_TASK=generate -P cmake\CafeTasks.cmake
exit /b %errorlevel%
:test
for %%I in ("%CAFE_CMAKE%") do set "CAFE_CTEST=%%~dpIctest.exe"
"%CAFE_CMAKE%" -E env --unset=BOTW_LOG_PATH --unset=BOTW_CAPTURE_DIR --unset=BOTW_AUDIO_SILENT_PLAYBACK BOTW_HEADLESS=1 BOTW_MUTE=1 "%CAFE_CTEST%" --test-dir "build\%CAFE_PRESET%" -C "%CAFE_CONFIG%" --output-on-failure -E botw_boot_10000
if errorlevel 1 goto failed
goto success
:package
"%CAFE_CMAKE%" -DCAFE_TASK=package -P cmake\CafeTasks.cmake
if errorlevel 1 goto failed
goto success
:usage_error
echo Invalid argument. Run build.bat help.
goto failed
:help
echo CafeRecomp: one build entry point, no PowerShell required.
echo   build.bat                     Build Release; generate sources if missing.
echo   build.bat build debug         Build Debug.
echo   build.bat configure           Configure without compiling.
echo   build.bat generate            Regenerate BOTW v208 sources and apply hooks.
echo   build.bat test [debug]        Test existing build, hidden and muted.
echo   build.bat package             Create a source-only ZIP, no game or binaries.
goto success
:failed
popd
exit /b 1
:success
popd
exit /b 0
