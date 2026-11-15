@echo off
chcp 65001 >nul
set PYTHONIOENCODING=utf-8
py "%~dp0tools\make_sounds.py"
echo.
pause
