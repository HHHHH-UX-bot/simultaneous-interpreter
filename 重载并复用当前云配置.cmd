@echo off
setlocal
cd /d "%~dp0"
if defined Z7_PYTHON (
  set "PYTHON_EXE=%Z7_PYTHON%"
) else if exist "%USERPROFILE%\miniconda3\envs\machine\python.exe" (
  set "PYTHON_EXE=%USERPROFILE%\miniconda3\envs\machine\python.exe"
) else if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" (
  set "PYTHON_EXE=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
) else (
  echo Python not found. Set Z7_PYTHON to your Python 3 executable.
  pause
  exit /b 1
)
"%PYTHON_EXE%" -B scripts\load_local_ai.py --reuse-runtime %*
pause
