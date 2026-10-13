# Creates a BBSC account on THIS PC's shadnet server (start the servers first).
$ErrorActionPreference = "Stop"
$server = Join-Path (Split-Path $PSScriptRoot -Parent) "server"
$key = (Get-Content (Join-Path $server "shadnet.cfg") | Where-Object { $_ -like "RegistrationSecretKey=*" }) -replace "^RegistrationSecretKey=", ""

Write-Host ""
Write-Host "  BBSC - NEW ACCOUNT on this server" -ForegroundColor Red
Write-Host ""
$name = (Read-Host "  Username (letters, numbers, - or _)").Trim()
$secure = Read-Host "  Password" -AsSecureString
$ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try { $pass = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
$mail = (Read-Host "  Email (any address, it is not checked)").Trim()

& (Join-Path $server "shadnet-sample.exe") 127.0.0.1 31313 register $name $pass $mail $key
$pass = $null
Write-Host ""
Write-Host "  If it says the account was created: in shadPS4 set shadNet ID = $name and that password." -ForegroundColor Cyan
Read-Host "`n  Press Enter to close"
