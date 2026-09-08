@echo off
cd /d "%~dp0"
py -2 -B run_module_build.py
exit /b %errorlevel%
