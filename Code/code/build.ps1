param(
    [string]$BuildRoot = (Join-Path $env:TEMP 'second_hand_platform_build'),
    [ValidateSet('MinGW Makefiles', 'Visual Studio 17 2022')]
    [string]$Generator = 'MinGW Makefiles'
)
$ErrorActionPreference = 'Stop'
if ($BuildRoot -match '[^\x00-\x7F]') {
    throw 'BuildRoot must use an ASCII path for the older MinGW tools.'
}
$source = Join-Path $BuildRoot 'source'
$build = Join-Path $BuildRoot 'build'
New-Item -ItemType Directory -Force -Path $source | Out-Null
# Stage an exact copy in an ASCII path; do not compile inside a Chinese path.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'include') -Destination $source -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src') -Destination $source -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests') -Destination $source -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'resources') -Destination $source -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Destination $source -Force
if ($Generator -eq 'Visual Studio 17 2022') {
    cmake -S $source -B $build -G $Generator -A x64
} else {
    cmake -S $source -B $build -G $Generator -DCMAKE_BUILD_TYPE=Release
}
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
$executable = Join-Path $build 'market_cli.exe'
if ($Generator -eq 'Visual Studio 17 2022') {
    $executable = Join-Path $build 'Release/market_cli.exe'
}
$bin = Join-Path (Split-Path $PSScriptRoot -Parent) 'bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
Copy-Item -LiteralPath $executable -Destination $bin -Force
$guiExecutable = Join-Path $build 'market_gui.exe'
if ($Generator -eq 'Visual Studio 17 2022') {
    $guiExecutable = Join-Path $build 'Release/market_gui.exe'
}
Copy-Item -LiteralPath $guiExecutable -Destination $bin -Force
Write-Host "Built and tested. Desktop: $bin/market_gui.exe ; CLI: $bin/market_cli.exe"
