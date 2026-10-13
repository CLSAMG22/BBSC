@echo off
rem For the player who is NOT hosting: connects your game to the host's servers.
rem Drag the host's card (BBSC-card-....json) onto this file, or double-click and paste the address.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\join-host.ps1" -Address "%~1"
