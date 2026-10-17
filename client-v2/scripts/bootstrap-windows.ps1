param([string]$Destination = (Join-Path $env:LOCALAPPDATA 'RuTool2Toolchain'))
$ErrorActionPreference = 'Stop'
$taskSource = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$taskTooling = Join-Path $Destination 'python-tools'
if (!(Test-Path -LiteralPath "$taskTooling/Scripts/python.exe")) {
    & python -m venv $taskTooling
    if ($LASTEXITCODE) { throw 'Python environment creation failed' }
}
$taskPython = Join-Path $taskTooling 'Scripts/python.exe'
& $taskPython -m pip install -r "$taskSource/scripts-tooling-lock.txt"
if ($LASTEXITCODE) { throw 'Pinned tooling installation failed' }
$taskSdk = Join-Path $Destination 'sdk'
if (!(Test-Path -LiteralPath "$taskSdk/6.8.3/mingw_64/bin/Qt6Quick.dll")) {
    & $taskPython -m aqt install-qt windows desktop 6.8.3 win64_mingw --outputdir $taskSdk --archives qtbase qtdeclarative qttools qtsvg
    if ($LASTEXITCODE) { throw 'Qt kit download/checksum verification failed' }
}
if (!(Test-Path -LiteralPath "$taskSdk/Tools/mingw1310_64/bin/g++.exe")) {
    & $taskPython -m aqt install-tool windows desktop tools_mingw1310 --outputdir $taskSdk
    if ($LASTEXITCODE) { throw 'MinGW kit download/checksum verification failed' }
}
$taskCppcheckSource = Join-Path $Destination 'cppcheck-2.22.0-source'
$taskCppcheckZip = Join-Path $Destination 'cppcheck-2.22.0-source.zip'
if (!(Test-Path -LiteralPath "$taskCppcheckSource/cppcheck-2.22.0/CMakeLists.txt")) {
    Invoke-WebRequest 'https://codeload.github.com/cppcheck-opensource/cppcheck/zip/refs/tags/2.22.0' -OutFile $taskCppcheckZip
    Expand-Archive -LiteralPath $taskCppcheckZip -DestinationPath $taskCppcheckSource
}
$taskPreviousPath = $env:PATH
try {
    $env:PATH = "$taskSdk/Tools/mingw1310_64/bin;$taskTooling/Scripts;$taskPreviousPath"
    & "$taskTooling/Scripts/cmake.exe" -S "$taskCppcheckSource/cppcheck-2.22.0" -B "$Destination/cppcheck-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=OFF -DBUILD_TESTING=OFF -DUSE_MATCHCOMPILER=ON
    if ($LASTEXITCODE) { throw 'Cppcheck configuration failed' }
    & "$taskTooling/Scripts/cmake.exe" --build "$Destination/cppcheck-build" -j 4
    if ($LASTEXITCODE) { throw 'Cppcheck compilation failed' }
} finally { $env:PATH = $taskPreviousPath }
Write-Output "SDK: $taskSdk"
Write-Output "Tooling: $taskTooling"
Write-Output "Cppcheck: $Destination/cppcheck-build/bin/cppcheck.exe"
