# Copies the shared code of the Horizon Client (repo cheatoskar/horizon-client, local clone "horizon-host")
# into vendor\horizon\, unchanged, and records the source commit.
#
#   powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -Commit <commit>   copy from that commit
#   powershell -ExecutionPolicy Bypass -File tools/sync-horizon.ps1 -Check             vendored files = recorded commit?
#
# The files are never edited here: a fix goes into horizon-host first, then this script brings it over.
# What is shared and why: vendor\horizon\SOURCE.md.
param(
    [string]$Repo = (Join-Path $PSScriptRoot "..\..\horizon-host"),
    [string]$Commit = "",
    [switch]$Check
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dest = Join-Path $root "vendor\horizon"
$stamp = Join-Path $dest "source.h"

# repo path -> path below vendor\horizon
$files = [ordered]@{
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

function HzGit([string[]]$a) {
    $out = & git.exe -C $Repo @a 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($a -join ' '): $out" }
    return $out
}

if ($Check) {
    $text = Get-Content $stamp -Raw
    if ($text -notmatch 'HORIZON_SOURCE_COMMIT "([0-9a-f]{40})"') { throw "no commit in $stamp" }
    $c = $Matches[1]
    $bad = 0
    foreach ($k in $files.Keys) {
        $want = (HzGit @("rev-parse", "${c}:$k")).Trim()
        $file = Join-Path $dest $files[$k]
        if (-not (Test-Path $file)) { Write-Host "MISSING  $($files[$k])"; $bad++; continue }
        # hashed with horizon-host's own line-ending rules, so a CRLF checkout still matches the blob
        $have = (HzGit @("hash-object", "--path=$k", $file)).Trim()
        if ($have -ne $want) { Write-Host "CHANGED  $($files[$k])"; $bad++ }
    }
    if ($bad) { Write-Host "$bad file(s) differ from horizon-host $c"; exit 1 }
    Write-Host "vendor\horizon: all $($files.Count) files = horizon-host $c"
    exit 0
}

if (-not $Commit) { throw "-Commit <commit> or -Check" }
$full = (HzGit @("rev-parse", "$Commit^{commit}")).Trim()
$branches = (HzGit @("branch", "--contains", $full, "--format=%(refname:short)")) -join ", "
$tmp = Join-Path $env:TEMP ("hz-sync-" + [guid]::NewGuid().ToString("N") + ".zip")
$paths = @($files.Keys)
HzGit (@("archive", "--format=zip", "-o", $tmp, $full, "--") + $paths) | Out-Null
$x = Join-Path $env:TEMP ("hz-sync-" + [guid]::NewGuid().ToString("N"))
Expand-Archive -Path $tmp -DestinationPath $x
foreach ($k in $files.Keys) {
    $to = Join-Path $dest $files[$k]
    New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
    Copy-Item -Force (Join-Path $x ($k -replace '/', '\')) $to
}
Remove-Item -Recurse -Force $x
Remove-Item -Force $tmp
$date = (HzGit @("show", "-s", "--format=%cs", $full)).Trim()
$subject = (HzGit @("show", "-s", "--format=%s", $full)).Trim() -replace '"', "'"
@"
// Written by tools/sync-horizon.ps1. The files in vendor\horizon\ are unchanged copies from this commit of
// horizon-host (cheatoskar/horizon-client); do not edit them here, fix them there and sync again.
#pragma once
#define HORIZON_SOURCE_COMMIT "$full"
#define HORIZON_SOURCE_SHORT "$($full.Substring(0, 7))"
#define HORIZON_SOURCE_DATE "$date"
// branches holding it when synced: $branches
// subject: $subject
"@ | Set-Content -Encoding ascii $stamp
Write-Host "synced $($files.Count) files from horizon-host $($full.Substring(0, 7)) ($date; $branches)"
