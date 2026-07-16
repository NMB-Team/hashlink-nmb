param(
    [string]$ManifestUrl = "https://github.com/NMB-Team/angle-nmb-builder/releases/download/nightly/latest.json",
    [string]$BuilderCommit = "",
    [string]$OutputDirectory = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$packageRoot = Join-Path $repositoryRoot "include\angle-nmb"
$cacheRoot = Join-Path $repositoryRoot ".cache\angle-nmb"
$builderCommitPath = Join-Path $repositoryRoot "other\angle-nmb-builder-commit.txt"
$platformKey = "windows-x64"
$expectedArtifactName = "angle-nmb-windows-x64.zip"
$requiredFiles = @(
    "ANGLE_REVISION",
    "VERSION",
    "BUILD_INFO.json",
    "LICENSE",
    "include\EGL\egl.h",
    "include\EGL\eglext.h",
    "include\GLES3\gl3.h",
    "bin\x64\libEGL.dll",
    "bin\x64\libGLESv2.dll",
    "lib\x64\libEGL.lib",
    "lib\x64\libGLESv2.lib"
)

if (-not $BuilderCommit) {
    $BuilderCommit = (Get-Content -LiteralPath $builderCommitPath -Raw).Trim()
}

function Assert-InRepository([string]$Path) {
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($repositoryRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "ANGLE path is outside the HashLink repository: $resolved"
    }
}

function Test-Package([string]$Root, [string]$ExpectedBuilderCommit = "") {
    foreach ($relativePath in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $relativePath) -PathType Leaf)) {
            return $false
        }
    }

    $revision = (Get-Content -LiteralPath (Join-Path $Root "ANGLE_REVISION") -Raw).Trim()
    if ($revision -notmatch "^[0-9a-fA-F]{40}$") {
        return $false
    }

    $buildInfo = Get-Content -LiteralPath (Join-Path $Root "BUILD_INFO.json") -Raw | ConvertFrom-Json
    if ($buildInfo.platform -ne "windows" -or $buildInfo.angleRevision -ne $revision) {
        return $false
    }
    if ($null -ne $buildInfo.renderer -and $buildInfo.renderer -ne "Vulkan") {
        return $false
    }
    if ($null -eq $buildInfo.targets -or $buildInfo.targets.Count -lt 1) {
        return $false
    }
    foreach ($target in $buildInfo.targets) {
        if ($target.renderer -ne "Vulkan") {
            return $false
        }
    }

    if ($ExpectedBuilderCommit) {
        $markerPath = Join-Path $Root ".angle-nmb-builder-commit"
        if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
            return $false
        }
        $installedCommit = (Get-Content -LiteralPath $markerPath -Raw).Trim()
        if (-not $installedCommit.StartsWith($ExpectedBuilderCommit, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $false
        }
    }

    return $true
}

function Write-RevisionHeader([string]$Root) {
    $revision = (Get-Content -LiteralPath (Join-Path $Root "ANGLE_REVISION") -Raw).Trim()
    $header = @"
#ifndef HL_ANGLE_REVISION
#define HL_ANGLE_REVISION "$revision"
#endif
"@
    [System.IO.File]::WriteAllText(
        (Join-Path $Root "angle_revision.h"),
        $header,
        [System.Text.UTF8Encoding]::new($false)
    )
}

function Copy-Runtime([string]$Root, [string]$Destination) {
    if (-not $Destination) {
        return
    }

    $outputRoot = [System.IO.Path]::GetFullPath($Destination)
    New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $Root "bin\x64\libEGL.dll") -Destination $outputRoot -Force
    Copy-Item -LiteralPath (Join-Path $Root "bin\x64\libGLESv2.dll") -Destination $outputRoot -Force
}

Assert-InRepository $packageRoot
Assert-InRepository $cacheRoot

if (-not $Force -and (Test-Package $packageRoot $BuilderCommit)) {
    Write-RevisionHeader $packageRoot
    Copy-Runtime $packageRoot $OutputDirectory
    Write-Host "Using cached ANGLE NMB package: $packageRoot"
    exit 0
}

if ($BuilderCommit) {
    if ($BuilderCommit.Length -lt 12 -or $BuilderCommit -notmatch "^[0-9a-fA-F]+$") {
        throw "BuilderCommit must contain at least 12 hexadecimal commit characters."
    }
    $shortCommit = $BuilderCommit.Substring(0, 12)
    $immutableManifestUrl = $ManifestUrl -replace "/releases/download/[^/]+/[^/]+$", "/releases/download/build-$shortCommit/manifest.json"
    if ($immutableManifestUrl -eq $ManifestUrl) {
        throw "ManifestUrl must end with a GitHub release tag and asset name when BuilderCommit is set."
    }
    $ManifestUrl = $immutableManifestUrl
}

if ($ManifestUrl -notmatch "^https://") {
    throw "ANGLE manifest URL must use HTTPS: $ManifestUrl"
}

New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$manifestPath = Join-Path $cacheRoot "manifest.json"
$manifestTemporaryPath = "$manifestPath.tmp"
Invoke-WebRequest -UseBasicParsing -Uri $ManifestUrl -OutFile $manifestTemporaryPath
Move-Item -LiteralPath $manifestTemporaryPath -Destination $manifestPath -Force

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported ANGLE manifest schema version '$($manifest.schemaVersion)'."
}
if ($manifest.builderCommit -notmatch "^[0-9a-fA-F]{40}$") {
    throw "ANGLE manifest contains an invalid builder commit."
}
$resolvedShortCommit = $manifest.builderCommit.Substring(0, 12)
if ($manifest.builderTag -ne "build-$resolvedShortCommit") {
    throw "ANGLE manifest builder tag does not match its builder commit."
}
if ($BuilderCommit -and -not $manifest.builderCommit.StartsWith($BuilderCommit, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "ANGLE manifest resolved builder commit '$($manifest.builderCommit)', not '$BuilderCommit'."
}

$artifact = $manifest.artifacts.$platformKey
if ($null -eq $artifact -or $artifact.name -ne $expectedArtifactName) {
    throw "ANGLE manifest does not contain the expected Windows x64 package."
}
if ($artifact.url -notmatch "^https://" -or $artifact.url -notmatch "/releases/download/$($manifest.builderTag)/$expectedArtifactName$") {
    throw "ANGLE artifact URL is not an immutable HTTPS release URL: $($artifact.url)"
}
if ($artifact.sha256 -notmatch "^[0-9a-fA-F]{64}$") {
    throw "ANGLE manifest contains an invalid archive SHA-256."
}

$artifactCacheRoot = Join-Path $cacheRoot "$resolvedShortCommit\$platformKey"
$archivePath = Join-Path $artifactCacheRoot $expectedArtifactName
$archiveTemporaryPath = "$archivePath.tmp"
$extractionRoot = Join-Path $artifactCacheRoot "package.tmp"
New-Item -ItemType Directory -Force -Path $artifactCacheRoot | Out-Null

if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
    $cachedHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if ($cachedHash -ne $artifact.sha256) {
        Remove-Item -LiteralPath $archivePath -Force
    }
}

if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    Invoke-WebRequest -UseBasicParsing -Uri $artifact.url -OutFile $archiveTemporaryPath
    $downloadedHash = (Get-FileHash -LiteralPath $archiveTemporaryPath -Algorithm SHA256).Hash
    if ($downloadedHash -ne $artifact.sha256) {
        Remove-Item -LiteralPath $archiveTemporaryPath -Force
        throw "ANGLE archive SHA-256 mismatch."
    }
    Move-Item -LiteralPath $archiveTemporaryPath -Destination $archivePath -Force
}

if (Test-Path -LiteralPath $extractionRoot) {
    Assert-InRepository $extractionRoot
    Remove-Item -LiteralPath $extractionRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $extractionRoot | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $extractionRoot

if (-not (Test-Package $extractionRoot)) {
    throw "Downloaded ANGLE package is incomplete or contains invalid metadata."
}
$packageRevision = (Get-Content -LiteralPath (Join-Path $extractionRoot "ANGLE_REVISION") -Raw).Trim()
if ($packageRevision -ne $manifest.angleRevision) {
    throw "ANGLE package revision '$packageRevision' does not match manifest revision '$($manifest.angleRevision)'."
}
[System.IO.File]::WriteAllText(
    (Join-Path $extractionRoot ".angle-nmb-builder-commit"),
    $manifest.builderCommit + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false)
)
Write-RevisionHeader $extractionRoot

if (Test-Path -LiteralPath $packageRoot) {
    Assert-InRepository $packageRoot
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
Move-Item -LiteralPath $extractionRoot -Destination $packageRoot

Copy-Runtime $packageRoot $OutputDirectory
Write-Host "Installed ANGLE NMB package: $packageRoot"
Write-Host "Builder commit: $($manifest.builderCommit)"
Write-Host "ANGLE revision: $packageRevision"
