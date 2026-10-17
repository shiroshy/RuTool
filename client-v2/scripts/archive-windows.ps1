param([Parameter(Mandatory=$true)][string]$ReleaseDirectory, [Parameter(Mandatory=$true)][string]$ArchivePath)
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
Write-Output "Verified complete archive: $taskArchive ($($taskFiles.Count) files)"
