# BBSC host: one double-click. First time: installs the C++ runtime and opens the firewall (one
# admin prompt). Every time: starts both servers and points this PC's game at them.
# Works on Radmin VPN, Hamachi, Tailscale, ZeroTier or a home network (tools\bbsc-network.ps1);
# -ChangeNetwork asks again which one friends use.
param([switch]$Setup, [switch]$ChangeNetwork)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$server = Join-Path $root "server"
. (Join-Path $PSScriptRoot "bbsc-network.ps1")
$rules = @(@("BBSC shadnet 31313", "TCP", 31313), @("BBSC matching 31314", "UDP", 31314),
           @("BBSC webapi 31315", "TCP", 31315), @("BBSC game server 20443", "TCP", 20443),
           @("BBSC relay 20444", "TCP", 20444), @("BBSC bbhost sessions 18671", "TCP", 18671),
           @("BBSC bbhost STUN 3478", "UDP", 3478))

function Test-VcRuntime {
    $key = "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"
    return (Test-Path $key) -and ((Get-ItemProperty $key -ErrorAction SilentlyContinue).Installed -eq 1)
}
# Every rule exists and lets in every VPN range (older packages made Radmin-only rules).
function Test-Firewall {
    foreach ($r in $rules) {
        $rule = Get-NetFirewallRule -DisplayName $r[0] -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $rule) { return $false }
        $have = @(($rule | Get-NetFirewallAddressFilter).RemoteAddress)
        foreach ($range in $BbscFirewallRanges) {
            $short = $range -replace '^(\d+)\.0\.0\.0/8$', '$1.0.0.0/255.0.0.0' -replace '^100\.64\.0\.0/10$', '100.64.0.0/255.192.0.0'
            if (-not ($have -contains $range -or $have -contains $short)) { return $false }
        }
    }
    return $true
}

# Elevated half: only runs once, when something is missing.
if ($Setup) {
    if (-not (Test-VcRuntime)) {
        Start-Process (Join-Path $server "vc_redist.x64.exe") -ArgumentList "/install /quiet /norestart" -Wait
    }
    foreach ($r in $rules) {
        if (Get-NetFirewallRule -DisplayName $r[0] -ErrorAction SilentlyContinue) {
            Set-NetFirewallRule -DisplayName $r[0] -RemoteAddress $BbscFirewallRanges
        } else {
            # VPN ranges and this PC's own networks only, so nothing is opened to the internet.
            New-NetFirewallRule -DisplayName $r[0] -Direction Inbound -Protocol $r[1] -LocalPort $r[2] `
                -RemoteAddress $BbscFirewallRanges -Action Allow | Out-Null
        }
    }
    exit 0
}

Write-Host ""
Write-Host "  BBSC HOST" -ForegroundColor Red
Write-Host "  ---------" -ForegroundColor DarkRed

$ip = Resolve-BbscAddress $server -Ask:$ChangeNetwork
if (-not $ip) {
    Write-Host "  [X] No network picked. Turn on Radmin VPN / Hamachi / Tailscale, then START-HOST again." -ForegroundColor Red
    Read-Host "`n  Press Enter to close"; exit 1
}
$kind = (Get-BbscNetworks | Where-Object { $_.Ip -eq $ip } | Select-Object -First 1).Kind
if (-not $kind) { $kind = "typed address" }
Write-Host "  Network: $kind  ($ip)   - to change it: START-HOST-OTHER-NETWORK.cmd" -ForegroundColor Gray

if (-not (Test-VcRuntime) -or -not (Test-Firewall)) {
    Write-Host "  First time: setting up (say Yes to the Windows prompt)..." -ForegroundColor Gray
    try {
        Start-Process powershell.exe -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList `
            "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Setup"
    } catch {
        Write-Host "  [X] Setup needs the Yes on the Windows prompt. Double-click START-HOST again." -ForegroundColor Red
        Read-Host "`n  Press Enter to close"; exit 1
    }
    if (Test-Firewall) {
        Write-Host "  [OK] Setup done" -ForegroundColor Green
    } else {
        Write-Host "  [!]  Firewall rules not complete - friends may not get in. Run START-HOST again." -ForegroundColor Yellow
    }
}

# First start of a fresh package: this host's own account registration key (NEW-ACCOUNT reads it).
$cfg = Join-Path $server "shadnet.cfg"
$cfgText = [IO.File]::ReadAllText($cfg)
if ($cfgText -match '(?m)^RegistrationSecretKey=\s*$') {
    $bytes = New-Object byte[] 20
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $newKey = -join ($bytes | ForEach-Object { $_.ToString("x2") })
    $cfgText = $cfgText -replace '(?m)^RegistrationSecretKey=\s*$', "RegistrationSecretKey=$newKey"
    [IO.File]::WriteAllText($cfg, $cfgText)
    Write-Host "  [OK] New server: own registration key created" -ForegroundColor Green
}

Get-Process shadnet, BBSC-GameServer -ErrorAction SilentlyContinue | Stop-Process -Force
$stamp = Get-Date -Format yyyyMMdd-HHmmss
New-Item -ItemType Directory -Force (Join-Path $server "logs") | Out-Null
Start-Process cmd.exe -WorkingDirectory $server -WindowStyle Minimized -ArgumentList `
    "/c title BBSC shadnet server & set SHADNET_BLOODBORNE_SEAMLESS_COOP=1 & set SHADNET_BLOODBORNE_RE_TRACE=1 & shadnet.exe > `"logs\shadnet-$stamp.log`" 2>&1"
Start-Sleep -Seconds 2
Start-Process (Join-Path $server "BBSC-GameServer.exe") -WorkingDirectory $server -WindowStyle Minimized -ArgumentList `
    "--shadnet $ip --host 0.0.0.0 --advertise $ip --summon-interval 5 --no-playlog --log-dir `"$server\logs`""
Write-Host "  [OK] Servers running (two minimized windows - leave them open while you play)" -ForegroundColor Green

# This PC's own game uses the servers too.
& (Join-Path $PSScriptRoot "join-host.ps1") -Address $ip -NoPause

$cards = @(Get-ChildItem (Join-Path $root "cards") -Filter *.json -ErrorAction SilentlyContinue |
    Where-Object { ((Get-Content $_.FullName -Raw | ConvertFrom-Json).shadnet -replace ':\d+$', '') -eq $ip })
Set-Clipboard -Value $ip -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "  ============================================" -ForegroundColor Cyan
Write-Host "    Send this to your friend:   $ip" -ForegroundColor White
Write-Host "    (already copied - just paste it in Discord)" -ForegroundColor Gray
Write-Host "    They double-click JOIN-HOST and paste it." -ForegroundColor Gray
if ($cards.Count -gt 0) {
    Write-Host "    Or DM them your card: cards\$($cards[0].Name)" -ForegroundColor Gray
} else {
    Write-Host "    Or make a card (address + key in one file): GENERATE-CARD" -ForegroundColor Gray
}
Write-Host "  ============================================" -ForegroundColor Cyan
Read-Host "`n  Press Enter to close this window (the servers keep running)"
