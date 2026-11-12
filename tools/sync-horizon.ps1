# Copies the shared code of the Horizon Client (repo cheatoskar/horizon-client, local clone "horizon-host")
# into vendor\horizon\, unchanged, and records the source commit of each group.
#
#   powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -Commit <commit>          login fix, controller fix, ImGui
#   powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -SettingsCommit <commit>  game settings (volumes, window mode)
#   powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -Check                    vendored files = recorded commits?
#
# Two groups because they come from two branches of horizon-host until those are merged there; once one commit
# holds both, sync both from it. The files are never edited here: a fix goes into horizon-host first, then this
# script brings it over. What is shared and why: README.md, "Code shared with the Horizon Client".
param(
    [string]$Repo = (Join-Path $PSScriptRoot "..\..\horizon-host"),
    [string]$Commit = "",
    [string]$SettingsCommit = "",
    [switch]$Check
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $root "vendor\horizon"
$stamp = Join-Path $dest "source.h"

# group -> (repo path -> path below vendor\horizon)
$groups = [ordered]@{
    "SOURCE" = [ordered]@{
        "src/host/onlinefix.cpp"                              = "host/onlinefix.cpp"
        "plugins/controller-hotplug/hp_bind.cpp"              = "controller-hotplug/hp_bind.cpp"
        "plugins/controller-hotplug/hp_bind.h"                = "controller-hotplug/hp_bind.h"
        "plugins/controller-hotplug/hp_enum.cpp"              = "controller-hotplug/hp_enum.cpp"
        "plugins/controller-hotplug/hp_enum.h"                = "controller-hotplug/hp_enum.h"
        "plugins/controller-hotplug/hp_game.cpp"              = "controller-hotplug/hp_game.cpp"
        "plugins/controller-hotplug/hp_game.h"                = "controller-hotplug/hp_game.h"
        "plugins/controller-hotplug/hp_service.cpp"           = "controller-hotplug/hp_service.cpp"
        "plugins/controller-hotplug/hp_service.h"             = "controller-hotplug/hp_service.h"
        "plugins/controller-hotplug/hp_tracker.cpp"           = "controller-hotplug/hp_tracker.cpp"
        "plugins/controller-hotplug/hp_tracker.h"             = "controller-hotplug/hp_tracker.h"
        "plugins/controller-hotplug/hp_watch.cpp"             = "controller-hotplug/hp_watch.cpp"
        "plugins/controller-hotplug/hp_watch.h"               = "controller-hotplug/hp_watch.h"
        "plugins/controller-hotplug/tests/hp_tests.cpp"       = "controller-hotplug/tests/hp_tests.cpp"
        "third_party/imgui/LICENSE.txt"                       = "imgui/LICENSE.txt"
        "third_party/imgui/imconfig.h"                        = "imgui/imconfig.h"
        "third_party/imgui/imgui.cpp"                         = "imgui/imgui.cpp"
        "third_party/imgui/imgui.h"                           = "imgui/imgui.h"
        "third_party/imgui/imgui_draw.cpp"                    = "imgui/imgui_draw.cpp"
        "third_party/imgui/imgui_internal.h"                  = "imgui/imgui_internal.h"
        "third_party/imgui/imgui_tables.cpp"                  = "imgui/imgui_tables.cpp"
        "third_party/imgui/imgui_widgets.cpp"                 = "imgui/imgui_widgets.cpp"
        "third_party/imgui/imstb_rectpack.h"                  = "imgui/imstb_rectpack.h"
        "third_party/imgui/imstb_textedit.h"                  = "imgui/imstb_textedit.h"
        "third_party/imgui/imstb_truetype.h"                  = "imgui/imstb_truetype.h"
        "third_party/imgui/backends/imgui_impl_dx9.cpp"       = "imgui/backends/imgui_impl_dx9.cpp"
        "third_party/imgui/backends/imgui_impl_dx9.h"         = "imgui/backends/imgui_impl_dx9.h"
        "third_party/imgui/backends/imgui_impl_win32.cpp"     = "imgui/backends/imgui_impl_win32.cpp"
        "third_party/imgui/backends/imgui_impl_win32.h"       = "imgui/backends/imgui_impl_win32.h"
    }
    "SETTINGS" = [ordered]@{
        "plugins/game-settings/gs_game.cpp"                   = "game-settings/gs_game.cpp"
        "plugins/game-settings/gs_game.h"                     = "game-settings/gs_game.h"
        "plugins/game-settings/gs_master.cpp"                 = "game-settings/gs_master.cpp"
        "plugins/game-settings/gs_master.h"                   = "game-settings/gs_master.h"
        "plugins/game-settings/tests/gs_tests.cpp"            = "game-settings/tests/gs_tests.cpp"
    }
}

function HzGit([string[]]$a) {
    $out = & git.exe -C $Repo @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($a -join ' '): $out" }
    return $out
}

# the recorded commit of each group, from source.h ("" if none yet)
function Recorded() {
    $r = @{}
    $text = if (Test-Path $stamp) { Get-Content $stamp -Raw } else { "" }
    foreach ($g in $groups.Keys) {
        $r[$g] = if ($text -match "HORIZON_${g}_COMMIT ""([0-9a-f]{40})""") { $Matches[1] } else { "" }
    }
    return $r
}

if ($Check) {
    $rec = Recorded
    $bad = 0
    $n = 0
    foreach ($g in $groups.Keys) {
        $c = $rec[$g]
        if (-not $c) { Write-Host "no HORIZON_${g}_COMMIT in $stamp"; $bad++; continue }
        $files = $groups[$g]
        foreach ($k in $files.Keys) {
            $n++
            $want = (HzGit @("rev-parse", "${c}:$k")).Trim()
            $file = Join-Path $dest $files[$k]
            if (-not (Test-Path $file)) { Write-Host "MISSING  $($files[$k])"; $bad++; continue }
            # hashed with horizon-host's own line-ending rules, so a CRLF checkout still matches the blob
            $have = (HzGit @("hash-object", "--path=$k", $file)).Trim()
            if ($have -ne $want) { Write-Host "CHANGED  $($files[$k]) (vs $($c.Substring(0, 7)))"; $bad++ }
        }
    }
    if ($bad) { Write-Host "$bad problem(s) in vendor\horizon"; exit 1 }
    Write-Host ("vendor\horizon: all $n files = horizon-host " + (($groups.Keys | ForEach-Object { $rec[$_].Substring(0, 7) }) -join " / "))
    exit 0
}

if (-not $Commit -and -not $SettingsCommit) { throw "-Commit <commit>, -SettingsCommit <commit> or -Check" }
$rec = Recorded
$want = @{ "SOURCE" = $Commit; "SETTINGS" = $SettingsCommit }
$info = @{}
foreach ($g in $groups.Keys) {
    $c = if ($want[$g]) { $want[$g] } else { $rec[$g] }
    if (-not $c) { throw "no commit for group $g (first sync: give it)" }
    $full = (HzGit @("rev-parse", "$c^{commit}")).Trim()
    $info[$g] = @{
        full     = $full
        date     = (HzGit @("show", "-s", "--format=%cs", $full)).Trim()
        subject  = (HzGit @("show", "-s", "--format=%s", $full)).Trim() -replace '"', "'"
        branches = (HzGit @("branch", "--contains", $full, "--format=%(refname:short)")) -join ", "
    }
    if (-not $want[$g]) { continue } # this group stays as it is
    $files = $groups[$g]
    $tmp = Join-Path $env:TEMP ("hz-sync-" + [guid]::NewGuid().ToString("N") + ".zip")
    HzGit (@("archive", "--format=zip", "-o", $tmp, $full, "--") + @($files.Keys)) | Out-Null
    $x = Join-Path $env:TEMP ("hz-sync-" + [guid]::NewGuid().ToString("N"))
    Expand-Archive -Path $tmp -DestinationPath $x
    foreach ($k in $files.Keys) {
        $to = Join-Path $dest $files[$k]
        New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
        Copy-Item -Force (Join-Path $x ($k -replace '/', '\')) $to
    }
    Remove-Item -Recurse -Force $x
    Remove-Item -Force $tmp
    Write-Host "synced $($files.Count) files ($g) from horizon-host $($full.Substring(0, 7)) ($($info[$g].date); $($info[$g].branches))"
}
$s = $info["SOURCE"]
$t = $info["SETTINGS"]
@"
// Written by tools/sync-horizon.ps1. The files in vendor\horizon\ are unchanged copies from these commits of
// horizon-host (cheatoskar/horizon-client); do not edit them here, fix them there and sync again.
#pragma once
// login fix, controller fix, Dear ImGui
#define HORIZON_SOURCE_COMMIT "$($s.full)"
#define HORIZON_SOURCE_SHORT "$($s.full.Substring(0, 7))"
#define HORIZON_SOURCE_DATE "$($s.date)"
// branches holding it when synced: $($s.branches)
// subject: $($s.subject)
// game settings (volumes, window mode)
#define HORIZON_SETTINGS_COMMIT "$($t.full)"
#define HORIZON_SETTINGS_SHORT "$($t.full.Substring(0, 7))"
#define HORIZON_SETTINGS_DATE "$($t.date)"
// branches holding it when synced: $($t.branches)
// subject: $($t.subject)
"@ | Set-Content -Encoding ascii $stamp
