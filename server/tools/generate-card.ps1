# BBSC host: makes a server card - one small file friends load in the BBSC launcher
# (Settings > Accounts > Import Card / Open Cards Folder) to get this server's address and
# registration key without typing them.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$server = Join-Path $root "server"
$cards = Join-Path $root "cards"

Write-Host ""
Write-Host "  BBSC - SERVER CARD" -ForegroundColor Red
Write-Host "  ------------------" -ForegroundColor DarkRed
Write-Host ""

# 1. Name
$default = "$env:USERNAME's BBSC server"
$name = (Read-Host "  Server name (Enter = $default)").Trim()
if (-not $name) { $name = $default }

# 2. Address: the one START-HOST uses (server\host-address.txt); Enter keeps it
. (Join-Path $PSScriptRoot "bbsc-network.ps1")
$current = Get-BbscSavedAddress $server
$address = ""
if ($current) {
    $answer = (Read-Host "  Address friends use: $current  (Enter = keep, C = change)").Trim()
    if ($answer -notmatch '^[cC]') { $address = $current }
}
if (-not $address) {
    $address = Select-BbscAddress $current
    if (-not $address) { exit 1 }
    if ($address -ne $current) {
        Save-BbscAddress $server $address
        if (Get-Process shadnet, BBSC-GameServer -ErrorAction SilentlyContinue) {
            Write-Host "  [!] The servers are running on $current - close them and double-click START-HOST" -ForegroundColor Yellow
            Write-Host "      again so they use $address too." -ForegroundColor Yellow
        }
    }
}
$kind = (Get-BbscNetworks | Where-Object { $_.Ip -eq $address } | Select-Object -First 1).Kind
if (-not $kind) { $kind = "Typed" }
$address = "${address}:31313"

# 3. Registration key (START-HOST makes it the first time)
$cfg = Join-Path $server "shadnet.cfg"
$key = ""
if (Test-Path $cfg) {
    $line = Get-Content $cfg | Where-Object { $_ -like "RegistrationSecretKey=*" } | Select-Object -First 1
    if ($line) { $key = ($line -replace '^RegistrationSecretKey=', '').Trim() }
}
if (-not $key) {
    Write-Host ""
    Write-Host "  [!] This server has no registration key yet. Run START-HOST once, then make the card" -ForegroundColor Yellow
    Write-Host "      again - without a key anyone who reaches the server can make accounts." -ForegroundColor Yellow
    if ((Read-Host "  Make a card without a key anyway? (y/N)").Trim() -notmatch '^[yY]') { exit 1 }
}

# 4. The card
New-Item -ItemType Directory -Force $cards | Out-Null
$safe = ($name -replace '[^A-Za-z0-9 _-]', '').Trim() -replace '\s+', '-'
if (-not $safe) { $safe = "server" }
$file = Join-Path $cards "BBSC-card-$safe.json"
$card = [ordered]@{
    name             = $name
    shadnet          = $address
    registration_key = $key
    network          = $kind
    created          = (Get-Date -Format "yyyy-MM-dd")
}
[IO.File]::WriteAllText($file, ($card | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))

Write-Host ""
Write-Host "  [OK] Card made: $file" -ForegroundColor Green

Write-Host ""
Write-Host "  ============================================" -ForegroundColor Cyan
Write-Host "    Send the card file to your friends (Discord DM)." -ForegroundColor White
Write-Host "    PRIVATE: it holds your registration key - anyone with it" -ForegroundColor White
Write-Host "    can make accounts on your server. Never post it publicly." -ForegroundColor White
Write-Host "    Friends: drag it onto JOIN-HOST.cmd (connects the game and adds" -ForegroundColor Gray
Write-Host "    the card), then launcher > Settings > Accounts > Create Account." -ForegroundColor Gray
Write-Host "  ============================================" -ForegroundColor Cyan
Start-Process explorer.exe "/select,`"$file`""
Read-Host "`n  Press Enter to close"
