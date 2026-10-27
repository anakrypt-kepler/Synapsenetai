# Stage the node and its DLLs, then build a one-file NSIS setup.
# The setup installs the desktop app for the current user and pulls WebView2 if it is missing.
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root "tauri-app\src-tauri\windows-runtime"
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$vcpkg = "C:\Project\Os\vcpkg\installed\x64-windows\bin"
$crt = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT"
$node = Join-Path $root "build\Release\synapsed.dll"
if (-not (Test-Path $node)) { throw "Missing $node. Build synapsed.dll first." }

Copy-Item $node (Join-Path $stage "synapsed.dll") -Force
foreach ($name in @("libcrypto-3-x64.dll", "libsodium.dll", "sqlite3.dll")) {
    $src = Join-Path $vcpkg $name
    if (-not (Test-Path $src)) { throw "Missing $src" }
    Copy-Item $src (Join-Path $stage $name) -Force
}
Get-ChildItem $crt -Filter "*.dll" | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $stage $_.Name) -Force
}
$omp = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\onecore\x64\Microsoft.VC145.OpenMP\vcomp140.dll"
if (-not (Test-Path $omp)) { throw "Missing $omp" }
Copy-Item $omp (Join-Path $stage "vcomp140.dll") -Force
$llamaBin = Join-Path $root "build\bin\Release"
foreach ($name in @("llama.dll", "ggml.dll", "ggml-base.dll", "ggml-cpu.dll")) {
    $src = Join-Path $llamaBin $name
    if (-not (Test-Path $src)) { throw "Missing $src" }
    Copy-Item $src (Join-Path $stage $name) -Force
}
foreach ($src in @(
    (Join-Path $root "build\Release\oqs.dll"),
    (Join-Path $root "build\_deps\secp256k1-build\bin\Release\libsecp256k1-7.dll")
)) {
    if (-not (Test-Path $src)) { throw "Missing $src" }
    Copy-Item $src (Join-Path $stage (Split-Path $src -Leaf)) -Force
}
$example = Join-Path $root "synapsenet.conf.example"
if (-not (Test-Path $example)) { throw "Missing $example" }
Copy-Item $example (Join-Path $stage "synapsenet.conf.example") -Force

$app = Join-Path $root "tauri-app"
Push-Location $app
try {
    # runtime-resources.json is applied only here. CI cargo builds must not
    # require DLLs that are not stored in git.
    & npm run tauri -- build --bundles nsis --config src-tauri/windows/runtime-resources.json
    if ($LASTEXITCODE -ne 0) { throw "tauri build failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

$setup = Get-ChildItem (Join-Path $app "src-tauri\target\release\bundle\nsis\*-setup.exe") |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $setup) { throw "NSIS setup was not produced." }
$dest = Join-Path $env:USERPROFILE "Desktop\SynapseNet-Setup.exe"
Copy-Item $setup.FullName $dest -Force
Write-Output "SETUP $($setup.FullName)"
Write-Output "DESKTOP $dest"
