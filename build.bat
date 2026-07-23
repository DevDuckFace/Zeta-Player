@echo off
REM ============================================================
REM  Zeta Player - build script
REM  Compiles the app and deploys it into the "bin" folder.
REM ============================================================
setlocal

REM --- Adjust these paths if your Qt is installed elsewhere ---
set "QT_DIR=C:\Qt\6.10.1\mingw_64"
set "QT_TOOLS=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin"

set "PATH=%QT_TOOLS%;%PATH%"

REM Project root without trailing backslash (avoids quoting problems)
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

echo.
echo === Configuring ===
cmake -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QT_DIR% -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 goto :error

echo.
echo === Building ===
cmake --build "%ROOT%\build"
if errorlevel 1 goto :error

echo.
echo === Deploying to bin ===
copy /Y "%ROOT%\build\ProjetoZeta.exe" "%ROOT%\bin\ProjetoZeta.exe" >nul
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations "%ROOT%\bin\ProjetoZeta.exe"
if errorlevel 1 goto :error

echo.
echo === Done ===
echo Run the app at:  %ROOT%\bin\ProjetoZeta.exe
goto :eof

:error
echo.
echo BUILD FAILED.
exit /b 1
