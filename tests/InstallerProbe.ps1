[CmdletBinding()]
param([Parameter(Mandatory)][string]$Installer)
$ErrorActionPreference = 'Stop'
function Require($value, [string]$message) {if (!$value) {throw $message}}
function RunSetup([string]$target) {
    $process = Start-Process -FilePath $Installer -ArgumentList "/S /D=$target" -PassThru -Wait -WindowStyle Hidden
    return $process.ExitCode
}
function WaitForRemoval([string]$target) {
    $limit = (Get-Date).AddSeconds(15)
    while ((Test-Path -LiteralPath "$target\installed.ini") -and (Get-Date) -lt $limit) {Start-Sleep -Milliseconds 100}
    Require (!(Test-Path -LiteralPath "$target\installed.ini")) 'Uninstaller did not finish'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $projectRoot ('tmp\installer-probe-' + [guid]::NewGuid().ToString('N'))
$installA = Join-Path $testRoot '安裝 測試 A'
$installB = Join-Path $testRoot '安裝 測試 B'
$registryRoot = 'HKCU:\Software\EinzigViewerInstallerTest'
Require (!(Test-Path -LiteralPath $registryRoot)) 'Installer test registry already exists; inspect it before rerunning'
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
try {
    New-Item -Path "$registryRoot\Classes\.png\OpenWithProgids" -Force | Out-Null
    Set-Item -LiteralPath "$registryRoot\Classes\.png" -Value 'OtherViewer.Image'
    New-ItemProperty -LiteralPath "$registryRoot\Classes\.png\OpenWithProgids" -Name 'OtherViewer.Image' -Value '' | Out-Null
    Require ((RunSetup $installA) -eq 0) 'Install in Unicode/spaced path failed'
    foreach ($name in @('EinzigViewer.exe','nvngx_vsr.dll','nvngx_truehdr.dll','vcruntime140.dll','Uninstall.exe','installed.ini','LICENSES\miniz-LICENSE.txt','LICENSES\Vulkan-ABI-LICENSE.txt','ai\realcugan\realcugan-ncnn-vulkan.exe','ai\realcugan\models-se\up2x-no-denoise.bin','ai\realcugan\models-se\up4x-no-denoise.bin','ai\realcugan\licenses\ncnn-LICENSE.txt')) {
        Require (Test-Path -LiteralPath "$installA\$name") "Missing installed payload: $name"
    }
    Require (!(Test-Path -LiteralPath "$installA\portable.ini")) 'Installer must not enable portable mode'
    $capabilities = Get-ItemProperty -LiteralPath "$registryRoot\EinzigViewer\Capabilities\FileAssociations"
    Require ($capabilities.'.png' -eq 'EinzigViewer.Image' -and $capabilities.'.epub' -eq 'EinzigViewer.Epub') 'Default Apps capabilities missing'
    $open = (Get-Item -LiteralPath "$registryRoot\Classes\EinzigViewer.Image\shell\open\command").GetValue('')
    Require ($open -eq ('"' + $installA + '\EinzigViewer.exe" "%1"')) 'Open command must quote executable and image path'
    Require ((Get-Item -LiteralPath "$registryRoot\Classes\.png").GetValue('') -eq 'OtherViewer.Image') 'Installer changed the user default'
    Set-Content -LiteralPath "$installA\settings.ini" -Value '[user settings]'
    Set-Content -LiteralPath "$installA\my-image.txt" -Value 'user data'
    Require ((RunSetup $installA) -eq 0) 'In-place upgrade failed'
    Require ((Get-Content -LiteralPath "$installA\settings.ini" -Raw).Trim() -eq '[user settings]') 'Upgrade replaced settings'
    $portableFolder = Join-Path $testRoot 'Existing portable'
    New-Item -ItemType Directory -Path $portableFolder -Force | Out-Null
    Set-Content -LiteralPath "$portableFolder\portable.ini" -Value '[portable]'
    Require ((RunSetup $portableFolder) -eq 5) 'Installer must reject an existing portable folder'
    Require (!(Test-Path -LiteralPath "$portableFolder\installed.ini")) 'Rejected install changed portable folder'
    $lock = [IO.File]::Open("$installA\EinzigViewer.exe", [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {Require ((RunSetup $installA) -eq 2) 'Installer must reject an EXE locked by a running viewer'} finally {$lock.Dispose()}
    Require ((RunSetup $installB) -eq 0) 'Alternative installation path failed'
    $uninstall = Start-Process -FilePath "$installA\Uninstall.exe" -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
    Require ($uninstall.ExitCode -eq 0) 'Old uninstaller launcher failed'
    WaitForRemoval $installA
    Require ((Get-ItemProperty -LiteralPath "$registryRoot\EinzigViewer").InstallDir -eq $installB) 'Old uninstall removed newer installation registration'
    $uninstall = Start-Process -FilePath "$installB\Uninstall.exe" -ArgumentList '/S' -Wait -PassThru -WindowStyle Hidden
    Require ($uninstall.ExitCode -eq 0) 'Uninstaller launcher failed'
    WaitForRemoval $installB
    Require (!(Test-Path -LiteralPath "$registryRoot\EinzigViewer")) 'Application registration remains after uninstall'
    Require (!(Test-Path -LiteralPath "$registryRoot\Classes\EinzigViewer.Image")) 'ProgID remains after uninstall'
    Require (!(Test-Path -LiteralPath "$installB\EinzigViewer.exe")) 'Viewer remains after uninstall'
    Require (!(Test-Path -LiteralPath "$installB\ai")) 'Bundled AI runtime directories remain after uninstall'
    Require (Test-Path -LiteralPath "$installA\settings.ini") 'Uninstall deleted user settings'
    Require (Test-Path -LiteralPath "$installA\my-image.txt") 'Uninstall deleted unrelated user data'
    Require ((Get-Item -LiteralPath "$registryRoot\Classes\.png").GetValue('') -eq 'OtherViewer.Image') 'Uninstall deleted the existing default association'
    Require ((Get-ItemProperty -LiteralPath "$registryRoot\Classes\.png\OpenWithProgids").PSObject.Properties.Name -contains 'OtherViewer.Image') 'Uninstall deleted another viewer registration'
    Write-Output 'PASS: payload, Unicode paths, quoted associations, default preservation, upgrade, running-file lock, ownership-aware uninstall, user-data preservation.'
} finally {
    # This constant test branch is created only after verifying it did not exist.
    # The real Software/Classes and RegisteredApplications branches are untouched.
    if (Test-Path -LiteralPath $registryRoot) {Remove-Item -LiteralPath $registryRoot -Recurse -Force}
}
