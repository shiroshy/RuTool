param(
    [Parameter(Mandatory=$true)][string]$ReleaseDirectory,
    [Parameter(Mandatory=$true)][string]$KeysFile,
    [string]$BuildDirectory = (Join-Path $env:LOCALAPPDATA 'RuTool2Builds/Lite-Release'),
    [string]$Version = '2.0.8',
    [string]$SdkRoot,
    [string]$ToolingRoot,
    [string]$Cppcheck,
    [string]$ArchivePath,
    [switch]$SkipTests,
    [switch]$KeepTemporaryFiles
)
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path $PSScriptRoot -Parent
$taskRelease = [IO.Path]::GetFullPath($ReleaseDirectory)
$taskKeys = Get-Content -Raw -LiteralPath $KeysFile | ConvertFrom-Json
if ($taskKeys.FormatVersion -ne 1 -or $taskKeys.RutPassword -isnot [string] -or [string]::IsNullOrEmpty($taskKeys.RutPassword) -or [Text.Encoding]::UTF8.GetByteCount($taskKeys.RutPassword) -gt 4096) { throw 'Invalid installer keys file.' }
if ((Test-Path -LiteralPath $taskRelease) -and (Get-ChildItem -LiteralPath $taskRelease -Force | Select-Object -First 1)) { throw 'Use a fresh empty release directory.' }
$taskDeploy = Join-Path $env:LOCALAPPDATA ('RuTool2Builds/Deployment-' + [guid]::NewGuid().ToString('N'))
& (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration Release -BuildDirectory $BuildDirectory -InstallDirectory $taskDeploy -Version $Version -SdkRoot $SdkRoot -ToolingRoot $ToolingRoot -Cppcheck $Cppcheck -WithTests:(!$SkipTests)
if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory 'RuToolInstaller.dll'))) { throw 'Missing native payload' }
[IO.Directory]::CreateDirectory((Join-Path $taskRelease 'app/config')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $taskRelease 'updater')) | Out-Null
foreach ($taskItem in Get-ChildItem -LiteralPath (Join-Path $taskDeploy 'bin')) {
    if ($taskItem.Name -ne 'RuTool.exe') { Copy-Item -LiteralPath $taskItem.FullName -Destination (Join-Path $taskRelease 'app') -Recurse }
}
foreach ($taskItem in Get-ChildItem -LiteralPath $taskDeploy -Directory) {
    if ($taskItem.Name -ne 'bin') { Copy-Item -LiteralPath $taskItem.FullName -Destination (Join-Path $taskRelease 'app') -Recurse }
}
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'RuToolInstaller.dll') -Destination (Join-Path $taskRelease 'app/RuToolInstaller.dll')
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'RuToolInstaller.exe') -Destination (Join-Path $taskRelease 'RuToolInstaller.exe')
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'RuToolUpdater.exe') -Destination (Join-Path $taskRelease 'updater/RuToolUpdater.exe')
Copy-Item -LiteralPath $KeysFile -Destination (Join-Path $taskRelease 'app/config/installer-keys.json')
Copy-Item -LiteralPath (Join-Path $taskSource 'config/update-public.der') -Destination (Join-Path $taskRelease 'app/config/update-public.der')
foreach ($taskLegal in @('LICENSE', 'NOTICE')) { Copy-Item -LiteralPath (Join-Path $taskSource $taskLegal) -Destination (Join-Path $taskRelease $taskLegal) }
if (Test-Path -LiteralPath (Join-Path $taskSource 'PRIVACY-RU.md')) { Copy-Item -LiteralPath (Join-Path $taskSource 'PRIVACY-RU.md') -Destination (Join-Path $taskRelease 'PRIVACY-RU.md') }
Copy-Item -LiteralPath (Join-Path $taskSource 'licenses') -Destination (Join-Path $taskRelease 'app/licenses') -Recurse
if (Test-Path -LiteralPath (Join-Path $taskSource 'README-LITE-RU.md')) { Copy-Item -LiteralPath (Join-Path $taskSource 'README-LITE-RU.md') -Destination (Join-Path $taskRelease 'README.md') }
if (!$ArchivePath) { $ArchivePath = $taskRelease + '.zip' }
& (Join-Path $PSScriptRoot 'archive-windows.ps1') -ReleaseDirectory $taskRelease -ArchivePath $ArchivePath -KeepTemporaryFiles:$KeepTemporaryFiles
if (!$KeepTemporaryFiles) {
    $taskTemporaryBase = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'RuTool2Builds')) + [IO.Path]::DirectorySeparatorChar
    $taskResolvedDeploy = [IO.Path]::GetFullPath($taskDeploy)
    if (!$taskResolvedDeploy.StartsWith($taskTemporaryBase, [StringComparison]::OrdinalIgnoreCase)) { throw 'Deployment cleanup path is outside the temporary build directory.' }
    if ((Get-Item -LiteralPath $taskResolvedDeploy -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Deployment cleanup root is a link; preserved.' }
    if (@(Get-ChildItem -LiteralPath $taskResolvedDeploy -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Deployment cleanup contains a link; preserved.' }
    Remove-Item -LiteralPath $taskResolvedDeploy -Recurse -Force
}
Write-Output "Lightweight Windows release: $taskRelease"
