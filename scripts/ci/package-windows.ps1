param(
    [string]$BuildDir = "build/windows-x64-release"
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true
Set-StrictMode -Version Latest

$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$buildPath = (Resolve-Path (Join-Path $sourceDir $BuildDir)).Path
$version = (Get-Content -LiteralPath (Join-Path $buildPath "version.txt") -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid application version: $version"
}

$stageDir = Join-Path $buildPath "package"
$distDir = Join-Path $sourceDir "dist"
if (Test-Path -LiteralPath $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
cmake --install $buildPath --prefix $stageDir
$payloadDir = Join-Path $stageDir "bin"
$application = Join-Path $payloadDir "Chunfe.exe"
if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
    throw "CMake did not install $application"
}

# The QML sources are in the project root. Include their imports and the
# matching MinGW runtime in the installer payload.
windeployqt --release --compiler-runtime --qmldir $sourceDir $application
@"
[Paths]
Prefix=.
Plugins=.
QmlImports=qml
"@ | Set-Content -LiteralPath (Join-Path $payloadDir "qt.conf") -Encoding utf8
foreach ($required in @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6Quick.dll",
    "platforms/qwindows.dll", "qml/QtQuick/qmldir",
    "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $payloadDir $required) -PathType Leaf)) {
        throw "Deployment is missing $required"
    }
}

$compiler = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6/ISCC.exe"
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    $compiler = (Get-Command ISCC.exe -ErrorAction Stop).Source
}
& $compiler "/DAppVersion=$version" "/DStageDir=$payloadDir" "/DDistDir=$distDir" (Join-Path $sourceDir "packaging/windows/chunfe.iss")

$installer = Join-Path $distDir "chunfe-$version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer was not created: $installer"
}
$testDir = Join-Path ([System.IO.Path]::GetTempPath()) ("chunfe-package-" + [guid]::NewGuid().ToString("N"))
$installedDir = Join-Path $testDir "installed"
New-Item -ItemType Directory -Path $testDir | Out-Null
try {
    $install = Start-Process -FilePath $installer -ArgumentList @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-",
        "/DIR=`"$installedDir`"", "/LOG=`"$testDir/install.log`""
    ) -PassThru -Wait
    if ($install.ExitCode -ne 0) {
        Get-Content -LiteralPath (Join-Path $testDir "install.log")
        throw "Installer failed with exit code $($install.ExitCode)"
    }

    # Verify the installed files against the staged payload. The application
    # has no automatic-exit test mode, so packaging does not launch it.
    foreach ($file in Get-ChildItem -LiteralPath $payloadDir -File -Recurse) {
        $relativePath = [System.IO.Path]::GetRelativePath($payloadDir, $file.FullName)
        $installedFile = Join-Path $installedDir $relativePath
        if (-not (Test-Path -LiteralPath $installedFile -PathType Leaf)) {
            throw "Installation is missing $relativePath"
        }
        $expectedHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $installedHash = (Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash
        if ($installedHash -ne $expectedHash) {
            throw "Installed file differs from the payload: $relativePath"
        }
    }
} finally {
    $uninstaller = Join-Path $installedDir "unins000.exe"
    if (Test-Path -LiteralPath $uninstaller) {
        $uninstall = Start-Process -FilePath $uninstaller -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -PassThru -Wait
        if ($uninstall.ExitCode -ne 0) {
            Write-Warning "Test uninstall returned $($uninstall.ExitCode)"
        }
    }
    Remove-Item -LiteralPath $testDir -Recurse -Force
}
Write-Host "Verified installer contents (application startup not tested): $installer"
