param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$SdkRoot,
    [string]$ToolingRoot,
    [string]$Cppcheck,
    [string]$BuildDirectory,
    [string]$InstallDirectory,
    [string]$Version = '2.0.7',
    [switch]$WithTests,
    [switch]$Benchmarks
)
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path $PSScriptRoot -Parent
$taskWorkspace = Split-Path $taskSource -Parent
if (!$SdkRoot) { $SdkRoot = Join-Path $taskWorkspace 'work/rutool2-sdk' }
if (!$ToolingRoot) { $ToolingRoot = Join-Path $taskWorkspace 'work/rutool2-tooling' }
if (!$Cppcheck) { $Cppcheck = Join-Path $env:LOCALAPPDATA 'RuTool2Toolchain/cppcheck-2.22.0/cppcheck.exe' }
if (!$BuildDirectory) { $BuildDirectory = Join-Path $env:LOCALAPPDATA "RuTool2Builds/$Configuration" }
$taskQt = Join-Path $SdkRoot '6.8.3/mingw_64'
$taskCompiler = Join-Path $SdkRoot 'Tools/mingw1310_64/bin'
$taskCmake = Join-Path $ToolingRoot 'Scripts/cmake.exe'
$taskCtest = Join-Path $ToolingRoot 'Scripts/ctest.exe'
foreach ($taskRequired in @($taskQt, $taskCompiler, $taskCmake, $taskCtest, $Cppcheck)) {
    if (!(Test-Path -LiteralPath $taskRequired)) { throw "Missing toolchain path: $taskRequired" }
}
$taskPreviousPath = $env:PATH
try {
    $env:PATH = "$taskCompiler;$taskQt/bin;$ToolingRoot/Scripts;$taskPreviousPath"
    $taskTestFlag = if ($WithTests) { 'ON' } else { 'OFF' }
    $taskBenchmarkFlag = if ($Benchmarks) { 'ON' } else { 'OFF' }
    & $taskCmake -S $taskSource -B $BuildDirectory -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_PREFIX_PATH=$taskQt" "-DCPPCHECK_EXECUTABLE=$Cppcheck" '-DRUTOOL_STATIC_ANALYSIS=ON' '-DRUTOOL_WARNINGS_AS_ERRORS=ON' "-DRUTOOL_BENCHMARKS=$taskBenchmarkFlag" "-DRUTOOL_APP_VERSION=$Version" "-DBUILD_TESTING=$taskTestFlag"
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    & $taskCmake --build $BuildDirectory -j 4
    if ($LASTEXITCODE) { throw 'C++ compilation or static analysis failed' }
    if ($WithTests) {
        & $taskCtest --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE) { throw 'CTest failed; inspect test reports' }
    }
    if ($InstallDirectory) {
        & $taskCmake --install $BuildDirectory --prefix $InstallDirectory
        if ($LASTEXITCODE) { throw 'Qt deployment failed' }
    }
    Write-Output "PASS: $Configuration Windows build and Cppcheck (developer tests: $WithTests). Output: $BuildDirectory"
} finally { $env:PATH = $taskPreviousPath }
