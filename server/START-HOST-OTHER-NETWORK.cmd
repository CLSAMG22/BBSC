@echo off
rem Like START-HOST, but asks again which network your friends use (e.g. switched from Radmin to Hamachi).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start-host.ps1" -ChangeNetwork
