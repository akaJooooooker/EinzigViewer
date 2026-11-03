[CmdletBinding()]
param(
    [string]$MakeNsis,
    [string]$CrtDirectory,
    [string]$RtxSdkRoot,
    [switch]$TestInstaller
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$RtxSdkRoot) {$RtxSdkRoot = Join-Path $projectRoot '.sdk\rtx-video-1.1.0'}
$versionText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
if ($versionText -notmatch 'project\(EinzigViewer VERSION (\d+\.\d+\.\d+)') {throw 'Cannot read version'}
$appVersion = $Matches[1]
if (!$MakeNsis) {
    $candidate = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($candidate) {$MakeNsis = $candidate.Source}
    else {$MakeNsis = Join-Path $projectRoot 'tmp\tools\nsis-3.11\makensis.exe'}
}
if (!(Test-Path -LiteralPath $MakeNsis)) {throw 'Specify -MakeNsis with the NSIS compiler path'}
if (!$CrtDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -property installationPath
    $CrtDirectory = Get-ChildItem -Path "$vsRoot\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (!$CrtDirectory -or !(Test-Path -LiteralPath (Join-Path $CrtDirectory 'vcruntime140.dll'))) {throw 'Matching x64 Visual C++ redistributable DLLs not found'}
$binary = Join-Path $projectRoot 'out\EinzigViewer.exe'
if ((Get-Item -LiteralPath $binary).VersionInfo.FileVersion -ne $appVersion) {throw 'Build the matching Release viewer first'}
$dist = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$stage = Join-Path $projectRoot ("tmp\package-" + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage 'payload'
$portable = Join-Path $stage 'portable'
New-Item -ItemType Directory -Path "$payload\LICENSES",$portable -Force | Out-Null
foreach ($name in @('EinzigViewer.exe','nvngx_vsr.dll','nvngx_truehdr.dll')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "out\$name") -Destination $payload
}
Copy-Item -Path "$CrtDirectory\*.dll" -Destination $payload
foreach ($name in @('README.txt','THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $payload
}
Copy-Item -LiteralPath (Join-Path $RtxSdkRoot 'NVIDIA_RTX_Video_SDK_License.pdf') -Destination "$payload\LICENSES"
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\miniz\LICENSE') -Destination "$payload\LICENSES\miniz-LICENSE.txt"
foreach ($name in @('LICENSE.txt','NOTICE.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party\vulkan-abi\$name") -Destination "$payload\LICENSES\Vulkan-ABI-$name"
}
$aiRuntime = Join-Path $projectRoot 'out\ai\realcugan'
foreach ($name in @('realcugan-ncnn-vulkan.exe','vcomp140.dll','LICENSE','SOURCE.txt','models-se\up2x-no-denoise.bin','models-se\up2x-no-denoise.param','models-se\up4x-no-denoise.bin','models-se\up4x-no-denoise.param','licenses\Real-CUGAN-MIT.txt','licenses\ncnn-LICENSE.txt')) {
    if (!(Test-Path -LiteralPath (Join-Path $aiRuntime $name))) {throw "Missing required AI asset: $name"}
}
New-Item -ItemType Directory -Path "$payload\ai" -Force | Out-Null
Copy-Item -LiteralPath $aiRuntime -Destination "$payload\ai\realcugan" -Recurse
$uninstallList = Join-Path $stage 'uninstall-files.nsh'
$deleteLines = Get-ChildItem -LiteralPath $payload -File -Recurse | ForEach-Object {
    'Delete "$INSTDIR\' + [IO.Path]::GetRelativePath($payload, $_.FullName) + '"'
}
$deleteLines += Get-ChildItem -LiteralPath $payload -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
    'RMDir "$INSTDIR\' + [IO.Path]::GetRelativePath($payload, $_.FullName) + '"'
}
Set-Content -LiteralPath $uninstallList -Value $deleteLines -Encoding utf8
Copy-Item -Path "$payload\*" -Destination $portable -Recurse
Set-Content -LiteralPath "$portable\portable.ini" -Value '[portable]' -Encoding utf8
$zip = Join-Path $dist "EinzigViewer-$appVersion-win-x64.zip"
if (!$TestInstaller) {Compress-Archive -Path "$portable\*" -DestinationPath $zip -Force}
$suffix = if ($TestInstaller) {'-Setup-Test.exe'} else {'-Setup.exe'}
$installer = Join-Path $dist "EinzigViewer-$appVersion-win-x64$suffix"
$arguments = @('/V3','/WX','/INPUTCHARSET','UTF8','/OUTPUTCHARSET','UTF8',"/DAPP_VERSION=$appVersion","/DPAYLOAD_DIR=$payload","/DOUTPUT_FILE=$installer","/DUNINSTALL_LIST=$uninstallList")
if ($TestInstaller) {$arguments += '/DTEST_MODE'}
$arguments += (Join-Path $projectRoot 'packaging\EinzigViewer.nsi')
& $MakeNsis @arguments
if ($LASTEXITCODE -ne 0) {throw "NSIS failed: $LASTEXITCODE"}
Get-FileHash -LiteralPath $installer -Algorithm SHA256
if (!$TestInstaller) {Get-FileHash -LiteralPath $zip -Algorithm SHA256}
Write-Output "Local artifacts ready in $dist (not published)."
