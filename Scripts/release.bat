@echo off
REM Publish a VoxBrain update to your testers (bump version, tag, push).
REM Double-click this file, then follow the prompts.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0release.ps1"
pause
