@echo off
rem Starts both BBSC servers on this PC (Radmin VPN, Hamachi, Tailscale, ZeroTier or home network).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start-host.ps1"
