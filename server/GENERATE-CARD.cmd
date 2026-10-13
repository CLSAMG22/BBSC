@echo off
rem Makes a server card (address + registration key) to send to your friends.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\generate-card.ps1"
