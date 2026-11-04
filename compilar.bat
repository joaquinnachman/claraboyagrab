@echo off
rem Compila Claraboya Grab en Windows. Doble clic y listo.
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
  echo Falta CMake. Mira LEEME.md, paso "Compilar en tu PC".
  pause
  exit /b 1
)

where git >nul 2>nul
if errorlevel 1 (
  echo Falta Git. Mira LEEME.md, paso "Compilar en tu PC".
  pause
  exit /b 1
)

echo Configurando (la primera vez baja JUCE, tarda unos minutos)...
cmake -B build -A x64
if errorlevel 1 goto error

echo Compilando...
cmake --build build --config Release --target ClaraboyaGrab_VST3 ClaraboyaGrab_Standalone
if errorlevel 1 goto error

echo.
echo LISTO. Copia "Claraboya Grab.vst3" a C:\Program Files\Common Files\VST3
explorer "build\ClaraboyaGrab_artefacts\Release\VST3"
pause
exit /b 0

:error
echo.
echo Fallo la compilacion. Mira los mensajes de arriba.
pause
exit /b 1
