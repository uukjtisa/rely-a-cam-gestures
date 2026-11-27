<#
.SYNOPSIS
  Packages a built Rely-A-Cam-Gestures into build\installer\RelyACamGestures-Setup-<ver>.exe.

.DESCRIPTION
  Run by `build.bat package` (through CMake's package-installer target) after a Release build.
  1. Stages what the installer ships: the app, its camera DLL, every runtime DLL and Qt plugin
     folder beside it, the models, the Microsoft C++ runtime (app-local, so a PC without the
     VC++ redistributable still starts it), and the licences.
  2. Compiles installer\RelyACamGestures.iss with Inno Setup.

.PARAMETER BuildDir
  The CMake build folder (default: build).

.PARAMETER Config
  The configuration to package (default: Release).
#>
param([string]$BuildDir = 'build', [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$bin = Join-Path $BuildDir $Config

$cm = Get-Content "$root\CMakeLists.txt" -Raw
if ($cm -notmatch 'project\(rely-a-cam-gestures VERSION ([0-9.]+)') { throw 'No project VERSION in CMakeLists.txt.' }
$ver = $Matches[1]
Write-Host "Packaging Rely-A-Cam-Gestures $ver from $bin"

foreach ($f in 'Rely-A-Cam-Gestures.exe', 'Rely-A-Cam-Gestures-Camera.dll') {
    if (-not (Test-Path "$bin\$f")) { throw "No $f in $bin (build $Config first)." }
}
if (-not (Test-Path "$root\models\manifest.json")) { throw 'No models (run build.bat, which fetches them).' }

$out = Join-Path $BuildDir 'installer'
$stage = Join-Path $out 'stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage, "$stage\models", "$stage\licenses" | Out-Null

# The app and everything the build copied beside it (tests, import libraries and symbols stay).
Get-ChildItem $bin -File | Where-Object {
    ($_.Extension -in '.exe', '.dll') -and $_.Name -ne 'racg-tests.exe'
} | Copy-Item -Destination $stage
# Qt's plugin folders hold release and debug builds side by side (qwindows.dll, qwindowsd.dll) plus
# symbols: only the release DLLs ship.
foreach ($d in 'platforms', 'imageformats', 'styles', 'iconengines', 'tls') {
    if (-not (Test-Path "$bin\$d")) { continue }
    New-Item -ItemType Directory -Force "$stage\$d" | Out-Null
    $dlls = Get-ChildItem "$bin\$d" -File -Filter *.dll
    $names = $dlls | ForEach-Object { $_.BaseName }
    $dlls | Where-Object { -not ($_.BaseName -match '^(.+)d$' -and $names -contains $Matches[1]) } |
        Copy-Item -Destination "$stage\$d"
}
Get-ChildItem "$root\models" -File | Where-Object { $_.Extension -in '.onnx', '.json' } | Copy-Item -Destination "$stage\models"

# The Microsoft C++ runtime, app-local (the camera DLL links it statically and needs none).
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw 'No Microsoft.VC143.CRT under the Visual Studio install.' }
foreach ($f in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll') {
    if (Test-Path "$($crt.FullName)\$f") { Copy-Item "$($crt.FullName)\$f" $stage }
}

Copy-Item "$root\LICENSE" "$stage\licenses\Rely-A-Cam-Gestures-GPL-3.0.txt"
Copy-Item "$root\docs\THIRD-PARTY.md" "$stage\licenses\THIRD-PARTY.md"

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in "$env:USERPROFILE\scoop\apps\inno-setup\current\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 7\ISCC.exe") {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) { throw 'Inno Setup not found (scoop install inno-setup).' }
& $iscc /Q "/DAppVersion=$ver" "/DStage=$stage" "/DOutDir=$out" "$root\installer\RelyACamGestures.iss"
if ($LASTEXITCODE) { throw 'Installer build failed.' }

Write-Host ''
Write-Host "Done: $out\RelyACamGestures-Setup-$ver.exe"
