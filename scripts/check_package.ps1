# Smoke-checks an extracted Windows package of Atlas.
#
# usage: pwsh scripts/check_package.ps1 <extracted folder Atlas-<version>-windows-x64>
#
#   cli  the packaged bin\atlas-cli.exe runs examples\scheduler\load_use_original.s
#        on rv5-h-f (the packaged copy) and prints "Cycles -> 11"
#   gui  the packaged bin\Atlas.exe starts with QT_QPA_PLATFORM=offscreen and is
#        still running after 5 s. Atlas keeps its settings in the registry
#        (HKCU\Software\Atlas); the key is removed afterwards unless it was there
#        before. The working directory and APPDATA are a throwaway folder.
# Prints PASS or FAIL for each, and exits 1 if any failed.
param([Parameter(Mandatory = $true)][string]$Folder)
Set-StrictMode -Version Latest

if (-not (Test-Path $Folder -PathType Container)) { Write-Error "check_package: not a folder: $Folder"; exit 2 }
$Root    = (Resolve-Path $Folder).Path
$Cli     = Join-Path $Root 'bin\atlas-cli.exe'
$Gui     = Join-Path $Root 'bin\Atlas.exe'
$Example = Join-Path $Root 'examples\scheduler\load_use_original.s'
Write-Host "== checking $(Split-Path -Leaf $Root)"

$script:Results = @()
$script:Failed  = $false
function Record([string]$step, [string]$verdict, [string]$detail) {
    $line = '  {0,-4} {1}  {2}' -f $step, $verdict, $detail
    $script:Results += $line
    if ($verdict -ne 'PASS') { $script:Failed = $true }
    Write-Host "  $verdict  $step  $detail"
}

# A throwaway working directory (vm_state\ goes there) and APPDATA.
$Sandbox = Join-Path ([System.IO.Path]::GetTempPath()) ("atlas-check-" + [guid]::NewGuid().ToString('N'))
$Run = Join-Path $Sandbox 'run'
New-Item -ItemType Directory -Force $Run, (Join-Path $Sandbox 'appdata'), (Join-Path $Sandbox 'local') | Out-Null
$env:APPDATA         = Join-Path $Sandbox 'appdata'
$env:LOCALAPPDATA    = Join-Path $Sandbox 'local'
$env:QT_QPA_PLATFORM = 'offscreen'
$SettingsKey = 'HKCU:\Software\Atlas'
$HadSettings = Test-Path $SettingsKey

try {
    # --- cli -----------------------------------------------------------------
    if ((Test-Path $Cli) -and (Test-Path $Example)) {
        $out = Join-Path $Sandbox 'cli.out'
        $p = Start-Process -FilePath $Cli -ArgumentList "`"$Example`"", '--vm', 'rv5-h-f' -WorkingDirectory $Run `
                 -RedirectStandardOutput $out -RedirectStandardError (Join-Path $Sandbox 'cli.err') -NoNewWindow -PassThru
        $null = $p.Handle  # keeps ExitCode available after the wait
        if (-not $p.WaitForExit(60000)) { $p.Kill(); $code = 'timeout' } else { $code = $p.ExitCode }
        $text = (Get-Content -Raw $out -ErrorAction SilentlyContinue) + ''
        if ($code -eq 0 -and $text -match '(?m)Cycles +-> 11\s*$') {
            Record 'cli' 'PASS' 'atlas-cli load_use_original.s --vm rv5-h-f: Cycles -> 11'
        } else {
            $tail = (($text -split "`r?`n") | Where-Object { $_ } | Select-Object -Last 3) -join ' '
            Record 'cli' 'FAIL' "exit $code; output: $tail"
        }
    } else {
        Record 'cli' 'FAIL' 'missing bin\atlas-cli.exe or the example'
    }

    # --- gui -----------------------------------------------------------------
    if (Test-Path $Gui) {
        $p = Start-Process -FilePath $Gui -WorkingDirectory $Run -PassThru `
                 -RedirectStandardOutput (Join-Path $Sandbox 'gui.out') -RedirectStandardError (Join-Path $Sandbox 'gui.err')
        $null = $p.Handle
        if ($p.WaitForExit(5000)) {
            $tail = ((Get-Content (Join-Path $Sandbox 'gui.err') -ErrorAction SilentlyContinue) | Select-Object -Last 3) -join ' '
            Record 'gui' 'FAIL' "exited with $($p.ExitCode) before 5 s: $tail"
        } else {
            $p.Kill()
            $p.WaitForExit()
            Record 'gui' 'PASS' 'Atlas.exe offscreen: still running after 5 s'
        }
    } else {
        Record 'gui' 'FAIL' 'missing bin\Atlas.exe'
    }
} finally {
    Remove-Item -Recurse -Force $Sandbox -ErrorAction SilentlyContinue
    if (-not $HadSettings) { Remove-Item -Recurse -Force $SettingsKey -ErrorAction SilentlyContinue }
}

Write-Host ''
Write-Host "Package check: $(Split-Path -Leaf $Root)"
$script:Results | ForEach-Object { Write-Host $_ }
if ($script:Failed) { Write-Host 'RESULT: FAIL'; exit 1 } else { Write-Host 'RESULT: PASS'; exit 0 }
