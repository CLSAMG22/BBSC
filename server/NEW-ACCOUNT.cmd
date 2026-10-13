@echo off
rem Creates an account on this PC's server (START-HOST first).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\new-account.ps1"
