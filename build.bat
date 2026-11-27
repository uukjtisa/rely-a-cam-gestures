@echo off
rem Build Rely-A-Cam-Gestures: fetch + convert the models, configure, build, run the tests.
rem   build.bat            RelWithDebInfo build + tests
rem   build.bat release    Release build + tests
rem   build.bat package    Release build + tests + installer (needs Inno Setup 6)
setlocal
cd /d "%~dp0"

set CONFIG=RelWithDebInfo
if /i "%~1"=="release" set CONFIG=Release
if /i "%~1"=="package" set CONFIG=Release

rem --- the converter's Python: 3.10 or 3.11 (TensorFlow 2.15 has no wheel for newer ones) ---------
set PY=.deps\py\Scripts\python.exe
if not exist "%PY%" (
  echo Creating the model converter's Python in .deps\py ...
  py -3.11 -m venv .deps\py 2>nul || py -3.10 -m venv .deps\py || (
    echo Needs Python 3.10 or 3.11 for the model converter. & exit /b 1)
  "%PY%" -m pip install --quiet --upgrade pip
  "%PY%" -m pip install --quiet tensorflow==2.15.1 tf2onnx==1.16.1 onnx==1.15.0 ^
    onnxruntime==1.17.3 "numpy<2" protobuf==3.20.3 || exit /b 1
)
"%PY%" -I scripts\fetch_models.py --out models || exit /b 1

rem --- configure + build + test -----------------------------------------------------------------
if not exist build\CMakeCache.txt (
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
)
cmake --build build --config %CONFIG% || exit /b 1
build\%CONFIG%\racg-tests.exe || exit /b 1

if /i "%~1"=="package" (
  cmake --build build --config %CONFIG% --target package-installer || exit /b 1
)
echo.
echo Built: build\%CONFIG%\Rely-A-Cam-Gestures.exe
