param([Parameter(Mandatory=$true)][string]$ReleaseDirectory, [Parameter(Mandatory=$true)][string]$ArchivePath, [switch]$KeepTemporaryFiles)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath($ReleaseDirectory)
$taskArchive = [IO.Path]::GetFullPath($ArchivePath)
if (Test-Path -LiteralPath $taskArchive) { throw 'Archive already exists.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($taskRoot, $taskArchive, [IO.Compression.CompressionLevel]::Optimal, $false)
$taskCheck = Join-Path $env:LOCALAPPDATA ('RuTool2Builds/ArchiveCheck-' + [guid]::NewGuid().ToString('N'))
[IO.Compression.ZipFile]::ExtractToDirectory($taskArchive, $taskCheck)
$taskFiles = @(Get-ChildItem -LiteralPath $taskRoot -File -Recurse)
foreach ($taskFile in $taskFiles) {
    $taskRelative = $taskFile.FullName.Substring($taskRoot.Length + 1)
    $taskExtracted = Join-Path $taskCheck $taskRelative
    if (!(Test-Path -LiteralPath $taskExtracted) -or (Get-FileHash -LiteralPath $taskFile.FullName).Hash -ne (Get-FileHash -LiteralPath $taskExtracted).Hash) { throw "Archive mismatch: $taskRelative" }
}
if (@(Get-ChildItem -LiteralPath $taskCheck -File -Recurse).Count -ne $taskFiles.Count) { throw 'Archive file count mismatch.' }
if (!$KeepTemporaryFiles) {
    $taskTemporaryBase = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'RuTool2Builds')) + [IO.Path]::DirectorySeparatorChar
    $taskResolvedCheck = [IO.Path]::GetFullPath($taskCheck)
    if (!$taskResolvedCheck.StartsWith($taskTemporaryBase, [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive check cleanup path is outside the temporary build directory.' }
    if ((Get-Item -LiteralPath $taskResolvedCheck -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Archive check cleanup root is a link; preserved.' }
    if (@(Get-ChildItem -LiteralPath $taskResolvedCheck -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Archive check cleanup contains a link; preserved.' }
    Remove-Item -LiteralPath $taskResolvedCheck -Recurse -Force
}
Write-Output "Verified complete archive: $taskArchive ($($taskFiles.Count) files)"
