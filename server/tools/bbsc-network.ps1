# Shared by START-HOST and GENERATE-CARD: which networks friends can reach this PC on, and the one
# address the host uses (server\host-address.txt), so the servers, the host's own game and the
# card all point at the same place.

# The firewall rules START-HOST makes let in: Radmin VPN, Hamachi, Tailscale and the local
# networks of this PC (home Wi-Fi, ZeroTier). Nothing from the internet. playit.gg comes in
# through its own agent on this PC, so it needs no rule.
$BbscFirewallRanges = @("26.0.0.0/8", "25.0.0.0/8", "100.64.0.0/10", "LocalSubnet")

# Privacy VPNs (ProtonVPN, NordVPN...) hide this PC; friends cannot connect through them.
$BbscPrivacyVpn = 'Proton|Nord|Mullvad|Surfshark|Express|Windscribe|Private Internet|PIA|CyberGhost|IPVanish|hide\.me|TunnelBear|Cloudflare|WARP'

function Get-BbscKind($ip, $alias, $desc) {
    $o = $ip.Split('.') | ForEach-Object { [int]$_ }
    $both = "$alias $desc"
    if ($both -match 'Radmin' -or $o[0] -eq 26) { return "Radmin VPN" }
    if ($both -match 'Hamachi' -or $o[0] -eq 25) { return "Hamachi" }
    if ($both -match 'Tailscale' -or ($o[0] -eq 100 -and $o[1] -ge 64 -and $o[1] -le 127)) { return "Tailscale" }
    if ($both -match 'ZeroTier') { return "ZeroTier" }
    if ($both -match $BbscPrivacyVpn) { return $null }
    if ($o[0] -eq 10 -or ($o[0] -eq 192 -and $o[1] -eq 168) -or ($o[0] -eq 172 -and $o[1] -ge 16 -and $o[1] -le 31)) {
        if ($desc -match 'WireGuard|TAP-Windows|OpenVPN|Wintun|VPN') { return $null } # some other VPN tunnel
        return "Home network, same house only ($alias)"
    }
    return "Other ($alias)"
}

# Every address on this PC friends could use; VPNs first. Hidden privacy VPNs go to $script:BbscSkipped.
function Get-BbscNetworks {
    $script:BbscSkipped = @()
    @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
        ForEach-Object {
            $desc = (Get-NetAdapter -InterfaceIndex $_.InterfaceIndex -ErrorAction SilentlyContinue).InterfaceDescription
            $kind = Get-BbscKind $_.IPAddress $_.InterfaceAlias $desc
            if ($kind) {
                [pscustomobject]@{ Ip = $_.IPAddress; Kind = $kind }
            } else {
                $script:BbscSkipped += $_.InterfaceAlias
            }
        } | Sort-Object { if ($_.Kind -like "Home*" -or $_.Kind -like "Other*") { 1 } else { 0 } })
}

function Get-BbscSavedAddress([string]$server) {
    $file = Join-Path $server "host-address.txt"
    if (Test-Path $file) { return ([IO.File]::ReadAllText($file)).Trim() }
    return ""
}

function Save-BbscAddress([string]$server, [string]$address) {
    [IO.File]::WriteAllText((Join-Path $server "host-address.txt"), $address)
}

# Lists the networks and asks. Returns the address friends use (no port), or "" if cancelled.
function Select-BbscAddress([string]$current) {
    $found = Get-BbscNetworks
    Write-Host ""
    Write-Host "  How do your friends reach this PC?" -ForegroundColor Cyan
    for ($i = 0; $i -lt $found.Count; $i++) {
        $mark = if ($found[$i].Ip -eq $current) { "  (in use now)" } else { "" }
        Write-Host ("   {0}. {1,-44} {2}{3}" -f ($i + 1), $found[$i].Kind, $found[$i].Ip, $mark)
    }
    if ($script:BbscSkipped.Count -gt 0) {
        Write-Host ("   (not listed: {0} - a privacy VPN, friends cannot reach you through it)" -f ($script:BbscSkipped -join ", ")) -ForegroundColor DarkGray
    }
    $typeIndex = $found.Count + 1
    Write-Host ("   {0}. Type an address (a domain, a public IP with ports forwarded...)" -f $typeIndex)
    $pick = 0
    while ($pick -lt 1 -or $pick -gt $typeIndex) {
        $answer = (Read-Host "  Number").Trim()
        [int]::TryParse($answer, [ref]$pick) | Out-Null
    }
    if ($pick -lt $typeIndex) { return $found[$pick - 1].Ip }
    Write-Host ""
    Write-Host "  The address only, no port. Friends need ports 31313-31315, 20443 and 20444 on it" -ForegroundColor Gray
    Write-Host "  with the SAME numbers (a playit.gg tunnel with other port numbers will not work" -ForegroundColor Gray
    Write-Host "  for co-op - use Radmin VPN, Hamachi or Tailscale instead)." -ForegroundColor Gray
    $address = ""
    while (-not $address) { $address = ((Read-Host "  Address").Trim() -replace ':\d+$', '') }
    return $address
}

# The address START-HOST and GENERATE-CARD use: the saved one if this PC still has it (or it is a
# typed name), else the only VPN address, else ask.
function Resolve-BbscAddress([string]$server, [switch]$Ask) {
    $saved = Get-BbscSavedAddress $server
    $found = Get-BbscNetworks
    $valid = $saved -and (($saved -notmatch '^\d{1,3}(\.\d{1,3}){3}$') -or ($found.Ip -contains $saved))
    if ($valid -and -not $Ask) { return $saved }
    if (-not $Ask) {
        $vpn = @($found | Where-Object { $_.Kind -notlike "Home*" -and $_.Kind -notlike "Other*" })
        if ($vpn.Count -eq 1) { Save-BbscAddress $server $vpn[0].Ip; return $vpn[0].Ip }
        if ($saved -and -not $valid) {
            Write-Host "  [!] This PC no longer has $saved (VPN off?)." -ForegroundColor Yellow
        }
    }
    $address = Select-BbscAddress $saved
    if ($address) { Save-BbscAddress $server $address }
    return $address
}
