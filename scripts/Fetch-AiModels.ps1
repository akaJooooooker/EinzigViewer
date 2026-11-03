[CmdletBinding()]
param(
    [string]$DestinationDirectory,
    [switch]$Offline
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Developer-only, explicit fetch. The viewer never runs this script.
$version = '20220728'
$archiveName = 'realcugan-ncnn-vulkan-20220728-windows.zip'
$archiveUrl = 'https://github.com/nihui/realcugan-ncnn-vulkan/releases/download/20220728/realcugan-ncnn-vulkan-20220728-windows.zip'
$expectedSha256 = 'C6E08D46C11704B1E3A1ADA9DDD591CB5005F52F132136C8633BA25DEF400E01'
$expectedArchiveBytes = 45977449
$archivePrefix = 'realcugan-ncnn-vulkan-20220728-windows/'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$cacheDirectory = Join-Path $projectDirectory '.sdk\realcugan'
$selectedDirectory = Join-Path $cacheDirectory 'selected'
$archivePath = Join-Path $cacheDirectory $archiveName
$thirdPartyDirectory = Join-Path $projectDirectory 'third_party\realcugan'
if (!$DestinationDirectory) {
    $DestinationDirectory = Join-Path $projectDirectory 'out\ai\realcugan'
}
$DestinationDirectory = [IO.Path]::GetFullPath($DestinationDirectory)

# Names and uncompressed sizes were inspected in the hash-pinned official ZIP.
$requiredMembers = [ordered]@{
    'realcugan-ncnn-vulkan.exe' = 6656512
    'vcomp140.dll' = 181680
    'models-se/up2x-no-denoise.bin' = 2573648
    'models-se/up2x-no-denoise.param' = 4818
    'models-se/up4x-no-denoise.bin' = 2818348
    'models-se/up4x-no-denoise.param' = 5078
    'LICENSE' = 1093
    'README.md' = 5136
}
$licenseNames = @(
    'Real-CUGAN-MIT.txt',
    'ncnn-LICENSE.txt',
    'glslang-LICENSE.txt',
    'libwebp-COPYING.txt',
    'libwebp-PATENTS.txt',
    'Microsoft-Visual-C-Runtime-2015-2022.txt'
)
foreach ($name in $licenseNames) {
    $path = Join-Path $thirdPartyDirectory "licenses\$name"
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing retained license: $path" }
}
foreach ($name in @('README.md', 'LICENSE.txt')) {
    if (!(Test-Path -LiteralPath (Join-Path $thirdPartyDirectory $name) -PathType Leaf)) {
        throw "Missing third-party notice: $name"
    }
}

[void][IO.Directory]::CreateDirectory($cacheDirectory)
if (!(Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    if ($Offline) { throw "Offline mode requires the pinned archive at $archivePath" }
    $downloadPath = "$archivePath.download"
    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $downloadPath
    if ((Get-Item -LiteralPath $downloadPath).Length -ne $expectedArchiveBytes -or
        (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash -ne $expectedSha256) {
        throw "Downloaded Real-CUGAN archive does not match the pinned size/SHA-256: $downloadPath"
    }
    Move-Item -LiteralPath $downloadPath -Destination $archivePath
}
if ((Get-Item -LiteralPath $archivePath).Length -ne $expectedArchiveBytes -or
    (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $expectedSha256) {
    throw 'Cached Real-CUGAN archive does not match the pinned official size/SHA-256. No files were extracted.'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $members = @{}
    $inventory = foreach ($entry in $archive.Entries) {
        $name = $entry.FullName
        if (!$name.StartsWith($archivePrefix, [StringComparison]::Ordinal) -or
            $name.Contains('\') -or $name.Contains(':') -or
            @($name.Split('/') | Where-Object { $_ -eq '..' -or $_ -eq '.' }).Count -gt 0) {
            throw "Unexpected or unsafe archive member: $name"
        }
        if ($members.ContainsKey($name)) { throw "Duplicate archive member: $name" }
        $members.Add($name, $entry)
        "{0}`t{1}" -f $entry.Length, $name
    }
    # Inspect every member before creating an extraction directory or copying files.
    foreach ($member in $requiredMembers.GetEnumerator()) {
        $fullName = $archivePrefix + $member.Key
        if (!$members.ContainsKey($fullName) -or $members[$fullName].Length -ne $member.Value) {
            throw "Required archive member is missing or changed: $fullName"
        }
    }
    $inventory | Set-Content -LiteralPath (Join-Path $cacheDirectory 'archive-members.txt') -Encoding UTF8
    foreach ($member in $requiredMembers.GetEnumerator()) {
        $entry = $members[$archivePrefix + $member.Key]
        $selectedPath = Join-Path $selectedDirectory $member.Key
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $selectedPath))
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $selectedPath, $true)
    }
} finally {
    $archive.Dispose()
}

[void][IO.Directory]::CreateDirectory($DestinationDirectory)
foreach ($member in $requiredMembers.GetEnumerator()) {
    $targetName = $member.Key
    if ($targetName -eq 'README.md') { $targetName = 'README.upstream.md' }
    $targetPath = Join-Path $DestinationDirectory $targetName
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $targetPath))
    Copy-Item -LiteralPath (Join-Path $selectedDirectory $member.Key) -Destination $targetPath -Force
}
$runtimeLicenseDirectory = Join-Path $DestinationDirectory 'licenses'
[void][IO.Directory]::CreateDirectory($runtimeLicenseDirectory)
foreach ($name in $licenseNames) {
    Copy-Item -LiteralPath (Join-Path $thirdPartyDirectory "licenses\$name") -Destination (Join-Path $runtimeLicenseDirectory $name) -Force
}
Copy-Item -LiteralPath (Join-Path $thirdPartyDirectory 'README.md') -Destination (Join-Path $DestinationDirectory 'README.einzig.md') -Force
@(
    "Release: $version",
    "URL: $archiveUrl",
    "Bytes: $expectedArchiveBytes",
    "SHA256: $expectedSha256"
) | Set-Content -LiteralPath (Join-Path $DestinationDirectory 'SOURCE.txt') -Encoding UTF8

Write-Output "Real-CUGAN $version ready at $DestinationDirectory"
Write-Output "Model directory: $(Join-Path $DestinationDirectory 'models-se')"
Write-Output 'Only the executable, bundled dependency, 2x/4x no-denoise models and notices were copied. No global runtime was installed.'
