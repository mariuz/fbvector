# build_msi.ps1
param (
    [string]$BuildDir = "..\..\build",
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"

# Resolve absolute paths
$StageDir = New-Item -ItemType Directory -Force -Path "wix_stage"
$StageDirAbs = [System.IO.Path]::GetFullPath($StageDir.FullName)
$resolvedPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $PSScriptRoot $BuildDir }
$BuildDirAbs = [System.IO.Path]::GetFullPath($resolvedPath)

Write-Host "Staging files from $BuildDirAbs to $StageDirAbs..."

# Copy binaries from the release folder
Copy-Item "$BuildDirAbs\bin\Release\fbvector.dll" -Destination $StageDirAbs

# Run Candle
$wixPath = if ($env:WIX) { "${env:WIX}bin\" } else { "" }
Write-Host "Running Candle..."
& "${wixPath}candle.exe" -dVersion=$Version -dStageDir="$StageDirAbs" -o wix_stage\installer.wixobj installer.wxs

# Run Light
Write-Host "Running Light..."
& "${wixPath}light.exe" wix_stage\installer.wixobj -out "fbvector-$Version-x64.msi"

Write-Host "MSI Installer built successfully: fbvector-$Version-x64.msi"
