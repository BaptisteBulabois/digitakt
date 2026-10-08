param(
    [switch]$Install
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $ProjectRoot "build-windows"

Write-Host "Configuring Takt II for Windows x64..."
cmake -S $ProjectRoot -B $BuildDirectory -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Host "Building Takt II VST3..."
cmake --build $BuildDirectory --config Release --parallel 3
if ($LASTEXITCODE -ne 0) { throw "Compilation failed." }

Write-Host "Running tests..."
ctest --test-dir $BuildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed." }

$Plugin = Join-Path $BuildDirectory "TaktII_artefacts\Release\VST3\Takt II.vst3"
if (-not (Test-Path $Plugin)) { throw "The VST3 bundle was not created at $Plugin" }

if ($Install) {
    $DestinationRoot = Join-Path $env:CommonProgramFiles "VST3"
    New-Item -ItemType Directory -Force -Path $DestinationRoot | Out-Null
    Copy-Item -Recurse -Force $Plugin $DestinationRoot
    Write-Host "Installed in $DestinationRoot"
} else {
    Write-Host "VST3 ready: $Plugin"
    Write-Host "Run this script as administrator with -Install to copy it into Ableton Live's system VST3 folder."
}
