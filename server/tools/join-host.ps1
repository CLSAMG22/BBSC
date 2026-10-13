# Points this PC's Bloodborne (shadPS4) at a BBSC host's servers. Takes the host's address or the
# host's server card (GENERATE-CARD's BBSC-card-*.json - drag it onto JOIN-HOST.cmd). Works on
# every network the host package supports: Radmin VPN (26.x), Hamachi (25.x), Tailscale
# (100.64-127.x), ZeroTier / same house (192.168.x, 10.x, 172.16-31.x) and named addresses.
# Old settings are backed up next to the originals. A card is also copied into the settings'
# servers\ folder, so the BBSC launcher's Settings > Accounts lists it for Create Account.
#
# shadPS4 keeps its settings in a "user" folder in the folder it starts from when there is one
# (portable), otherwise in %APPDATA%\shadPS4. The redirect has to be in the one the game uses, or
# the game asks Sony's old server for its server list and says "Failed to acquire Bloodborne
# server information". So this writes it into every settings folder it can find, and asks for
# the shadPS4.exe of a portable install it cannot find by itself.
param([string]$Address = "", [string]$Card = "", [switch]$NoPause)
$ErrorActionPreference = "Stop"

function Done([int]$code) { if (-not $NoPause) { Read-Host "`n  Press Enter to close" }; exit $code }

function Get-NetworkKind([string]$a) {
    if ($a -notmatch '^\d{1,3}(\.\d{1,3}){3}$') {
        if ($a -match '^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$' -and $a -match '[A-Za-z]') { return "a named address" }
        return $null
    }
    if ($a -match '^26\.') { return "Radmin VPN" }
    if ($a -match '^25\.') { return "Hamachi" }
    if ($a -match '^100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.') { return "Tailscale" }
    if ($a -match '^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)') { return "ZeroTier or the same router / Wi-Fi" }
    if ($a -match '^(0|127)\.') { return $null }
    return "the internet (ports forwarded)"
}

if (-not $Address -and -not $Card) {
    Write-Host ""
    Write-Host "  BBSC - CONNECT TO A HOST" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Paste the host's address, or drag their card file (BBSC-card-....json) in here." -ForegroundColor Gray
    $Address = (Read-Host "  Address or card").Trim().Trim('"')
}
if (-not $Card -and $Address -match '\.json$') { $Card = $Address; $Address = "" }
$cardData = $null
if ($Card) {
    try { $cardData = Get-Content -LiteralPath $Card -Raw | ConvertFrom-Json } catch { }
    if (-not $cardData -or -not $cardData.shadnet) {
        Write-Host "  [X] '$Card' is not a BBSC server card." -ForegroundColor Red
        Done 1
    }
    $Address = [string]$cardData.shadnet
    Write-Host "  Card: $($cardData.name)  ($($cardData.network))" -ForegroundColor Gray
}
$port = 31313
if ($Address -match '^(.+):(\d+)$') { $Address = $Matches[1]; $port = [int]$Matches[2] }
$kind = Get-NetworkKind $Address
if (-not $kind) {
    Write-Host "  [X] '$Address' is not a host address. It looks like 26.x.x.x (Radmin), 25.x.x.x" -ForegroundColor Red
    Write-Host "      (Hamachi), 100.x.x.x (Tailscale), 192.168.x.x (same house) or a name." -ForegroundColor Red
    Done 1
}
if ($port -ne 31313) {
    Write-Host "  [!]  The host's shadNet port is $port, not 31313 (a playit.gg tunnel?). Accounts work," -ForegroundColor Yellow
    Write-Host "       but co-op also needs 31315, 20443 and 20444 on the same address - ask the host" -ForegroundColor Yellow
    Write-Host "       to use Radmin VPN, Hamachi or Tailscale if joining fails." -ForegroundColor Yellow
}
# Every shadPS4 settings folder we can find.
$folders = New-Object System.Collections.Generic.List[string]
function Add-Folder([string]$path) {
    if ($path -and (Test-Path $path) -and -not $folders.Contains((Resolve-Path $path).Path)) {
        $folders.Add((Resolve-Path $path).Path)
    }
}
Add-Folder (Join-Path $env:APPDATA "shadPS4")
Add-Folder (Join-Path (Split-Path $PSScriptRoot -Parent) "user")          # the BBSC game package
Get-Process shadPS4 -ErrorAction SilentlyContinue | ForEach-Object {       # a running shadPS4
    if ($_.Path) { Add-Folder (Join-Path (Split-Path $_.Path -Parent) "user") }
}
$exe = ""
if (-not $NoPause) {
    Write-Host ""
    Write-Host "  Portable shadPS4 (its own 'user' folder next to shadPS4.exe)? Drag that shadPS4.exe" -ForegroundColor Gray
    $exe = (Read-Host "  into this window and press Enter - or just press Enter").Trim().Trim('"')
}
if ($exe) {
    $exeDir = if (Test-Path $exe -PathType Container) { $exe } else { Split-Path $exe -Parent }
    $user = Join-Path $exeDir "user"
    if (-not (Test-Path $user)) { New-Item -ItemType Directory -Force $user | Out-Null }
    Add-Folder $user
}
if ($folders.Count -eq 0) {
    Write-Host "  [X] No shadPS4 settings found. Start the game once, then run this again." -ForegroundColor Red
    Done 1
}
$stamp = Get-Date -Format yyyyMMdd-HHmmss

foreach ($dir in $folders) {
    Write-Host ""
    Write-Host "  Settings in $dir" -ForegroundColor Cyan

    # 1. host_overrides.json: the game's server list and summon service go to the host.
    $overrides = Join-Path $dir "host_overrides.json"
    if (Test-Path $overrides) { Copy-Item $overrides "$overrides.bak-$stamp" }
    $json = "{`r`n  `"https://ss4.scej-network.jp:20443`": `"http://${Address}:20443`",`r`n  `"http://thehuntersdream.com:18671/summon_messenger`": `"http://${Address}:31315`"`r`n}`r`n"
    [IO.File]::WriteAllText($overrides, $json)
    Write-Host "  [OK] server list -> $Address ($kind)" -ForegroundColor Green

    # 2. Bloodborne's own shadNet server (per-game settings, CUSA03173).
    $game = Join-Path $dir "custom_configs\CUSA03173.json"
    $set = $false
    if (Test-Path $game) {
        $text = [IO.File]::ReadAllText($game)
        $new = $text -replace '"shadnet_server"\s*:\s*"[^"]*"', "`"shadnet_server`": `"${Address}:${port}`""
        $new = $new -replace '"shadnet_webapi_server"\s*:\s*"[^"]*"', "`"shadnet_webapi_server`": `"http://${Address}:31315`""
        if ($new -match [regex]::Escape("${Address}:${port}")) {
            if ($new -ne $text) { Copy-Item $game "$game.bak-$stamp"; [IO.File]::WriteAllText($game, $new) }
            Write-Host "  [OK] Bloodborne's shadNet server -> ${Address}:${port}" -ForegroundColor Green
            $set = $true
        }
    }
    # 3. The card, for the launcher's Settings > Accounts (Create Account on Server).
    if ($Card) {
        $servers = Join-Path $dir "servers"
        New-Item -ItemType Directory -Force $servers | Out-Null
        Copy-Item -LiteralPath $Card (Join-Path $servers (Split-Path $Card -Leaf)) -Force
        Write-Host "  [OK] card added - launcher: Settings > Accounts > pick it > Create Account" -ForegroundColor Green
    }
    if (-not $set) {
        Write-Host "  [!]  Check by hand: launcher -> Bloodborne settings -> shadNet server ${Address}:${port}," -ForegroundColor Yellow
        Write-Host "       WebAPI server http://${Address}:31315" -ForegroundColor Yellow
    }
}

# 4. Can we reach the host's two servers?
Write-Host ""
$ok = $true
try {
    $status = Invoke-RestMethod "http://${Address}:31315/status" -TimeoutSec 6
    if ($status.ok) { Write-Host "  [OK] shadNet server answers" -ForegroundColor Green }
} catch {
    $ok = $false
    Write-Host "  [X] shadNet server (${Address}:31315) does not answer." -ForegroundColor Red
}
try {
    $info = Invoke-WebRequest "http://${Address}:20443/bb-eu/ss.info" -TimeoutSec 6 -UseBasicParsing
    if ($info.StatusCode -eq 200) { Write-Host "  [OK] game server answers (server list)" -ForegroundColor Green }
} catch {
    $ok = $false
    Write-Host "  [X] game server (${Address}:20443) does not answer." -ForegroundColor Red
}
if ($ok) {
    Write-Host "  Ready - restart the game if it is open." -ForegroundColor Green
} else {
    Write-Host "      Is the host's START-HOST running, and are you both connected ($kind)?" -ForegroundColor Yellow
}
Done 0
