# Builds the Windows package of Atlas:
#   dist\Atlas-<version>-windows-x64.zip   (top folder Atlas-<version>-windows-x64\)
#
# usage: pwsh scripts/package_windows.ps1
#
# Needs cmake, ninja, a C++ compiler and Qt 6 (qmake and windeployqt) on
# PATH; CI uses Qt 6.9.2 win64_mingw with MinGW 13.1. A Release build in
# build-package\ (tests off) is installed into a staging folder, windeployqt
# adds the Qt libraries and plugins, and the folder is zipped.
# <version> is atlasVersion() with "+" made "-".
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Die([string]$msg) { Write-Error "package_windows: $msg"; exit 1 }
function Run([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { Die "$exe $($argv -join ' ') failed with exit $LASTEXITCODE" }
}

$Root  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = Join-Path $Root 'build-package'
$Dist  = Join-Path $Root 'dist'

foreach ($tool in 'cmake', 'ninja', 'windeployqt') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Die "'$tool' is required on PATH" }
}

# --- build and install ---------------------------------------------------
Write-Host '== configure (Release, tests off) in build-package\'
$configure = & cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF 2>&1
$configure | Select-Object -Last 3 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { $configure | ForEach-Object { Write-Host $_ }; Die 'configure failed' }
$line = $configure | ForEach-Object { "$_" } | Where-Object { $_ -match '^-- Atlas version ' } | Select-Object -Last 1
if (-not $line) { Die "the configure step did not print '-- Atlas version'" }
$AtlasVersion = ($line -replace '^-- Atlas version ', '').Trim()
$Version = $AtlasVersion -replace '\+', '-'
Write-Host "== version $AtlasVersion (files: $Version)"

Write-Host '== build'
Run cmake @('--build', $Build)

$Top   = "Atlas-$Version-windows-x64"
$Stage = Join-Path $Build 'stage'
$Pkg   = Join-Path $Stage $Top
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
Run cmake @('--install', $Build, '--prefix', $Pkg)
$Bin = Join-Path $Pkg 'bin'
foreach ($exe in 'Atlas.exe', 'atlas-cli.exe') {
    if (-not (Test-Path (Join-Path $Bin $exe))) { Die "install did not produce bin\$exe" }
}

# --- Qt and the compiler runtime -----------------------------------------
Write-Host '== windeployqt'
Run windeployqt @('--release', '--no-translations', '--compiler-runtime', (Join-Path $Bin 'Atlas.exe'))
Run windeployqt @('--release', '--no-translations', '--compiler-runtime', (Join-Path $Bin 'atlas-cli.exe'))
# atlas-cli runs on the offscreen platform plugin, which windeployqt leaves out.
$qmake = Get-Command qmake6, qmake -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $qmake) { Die "qmake (Qt 6) is required on PATH" }
$qtPlugins = (& $qmake.Source -query QT_INSTALL_PLUGINS).Trim()
$offscreen = Join-Path $qtPlugins 'platforms\qoffscreen.dll'
if (-not (Test-Path $offscreen)) { Die "no $offscreen" }
New-Item -ItemType Directory -Force (Join-Path $Bin 'platforms') | Out-Null
Copy-Item $offscreen (Join-Path $Bin 'platforms')
# windeployqt copies the MinGW runtime only when it can find it; copy it from
# the compiler's folder if it did not.
$gpp = Get-Command g++ -ErrorAction SilentlyContinue
if ($gpp) {
    $gccBin = Split-Path $gpp.Source
    foreach ($dll in 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') {
        $src = Join-Path $gccBin $dll
        if ((Test-Path $src) -and -not (Test-Path (Join-Path $Bin $dll))) { Copy-Item $src $Bin }
    }
}

# --- what goes with it ---------------------------------------------------
Copy-Item -Recurse (Join-Path $Root 'examples') (Join-Path $Pkg 'examples')
Copy-Item (Join-Path $Root 'LICENSE.md'), (Join-Path $Root 'NOTICE.md') $Pkg
@"
Atlas $AtlasVersion - Windows x64

Atlas is a RISC-V assembly simulator for learning programming and
microarchitecture.

Run
  Unzip the folder anywhere, then double-click bin\Atlas.exe.
  The command line (no window needed), from this folder:
    bin\atlas-cli.exe examples\scheduler\load_use_original.s --vm rv5-h-f

Inside
  bin\Atlas.exe, bin\atlas-cli.exe   the application and the command line
  bin\*.dll, bin\<plugin folders>    the bundled Qt 6 libraries and plugins,
                                     and the MinGW C++ runtime
  examples\                          example programs
  LICENSE.md, NOTICE.md              Atlas's licence and origin

The Qt 6 libraries are bundled under the GNU LGPL.
"@ -replace "`r?`n", "`r`n" | Set-Content -NoNewline -Encoding ascii (Join-Path $Pkg 'README_PACKAGE.txt')

# --- zip -----------------------------------------------------------------
New-Item -ItemType Directory -Force $Dist | Out-Null
$Zip = Join-Path $Dist "$Top.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Pkg -DestinationPath $Zip
Write-Host '== wrote'
Get-Item $Zip | Format-Table Name, Length -AutoSize | Out-String | Write-Host
