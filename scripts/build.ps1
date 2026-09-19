<#
.SYNOPSIS
    构建 Entray 的 Release 单文件版，并对产物做自检。

.DESCRIPTION
    会依次做三件事：
      1. 让 CMake 自己找本机装的 Visual Studio 来配置（不需要先跑 vcvars）；
      2. 编译指定配置；
      3. 用 tools/check_exe.py 检查产物确实不依赖任何运行库，然后把 exe 复制到 dist\。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1

.EXAMPLE
    # 只编译 32 位版本，跳过自检
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Platform x86 -SkipCheck
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64', 'x86')]
    [string]$Platform = 'x64',

    [switch]$SkipCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 自检脚本的输出带中文。这里把控制台输出编码也改成 UTF-8，两边对齐，
# 否则中文在 GBK 控制台里会显示成乱码。
$env:PYTHONIOENCODING = 'utf-8'
try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
}
catch {
    # 输出被重定向时改不了控制台编码，忽略即可
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build'
$distDir = Join-Path $repoRoot 'dist'

function Find-CMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $candidates = @(
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'),
        (Join-Path $programFilesX86 'CMake\bin\cmake.exe')
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw '找不到 cmake。请先安装 CMake（https://cmake.org/download/）并把它加入 PATH。'
}

$cmake = Find-CMake
Write-Host "CMake     : $cmake"
Write-Host "配置      : $Configuration / $Platform"
Write-Host ''

Write-Host '[1/4] 配置 CMake ...'
# 不指定 -G：让 CMake 自己挑本机装的 Visual Studio（2022 / 2026 都能用）。
# 写死成 'Visual Studio 17 2022' 的话，装了更新版本 VS 的机器会直接报
# “could not find any instance of Visual Studio”。
& $cmake -S $repoRoot -B $buildDir -A $Platform
if ($LASTEXITCODE -ne 0) {
    throw "CMake 配置失败（退出码 $LASTEXITCODE）。请确认已安装 Visual Studio（2022 或更新版本）及“使用 C++ 的桌面开发”工作负载。"
}

Write-Host ''
Write-Host '[2/4] 编译 ...'
& $cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "编译失败（退出码 $LASTEXITCODE）。"
}

$exePath = Join-Path $buildDir (Join-Path $Configuration 'Entray.exe')
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "编译结束但没有找到产物：$exePath"
}

Write-Host ''
Write-Host '[3/4] 校验产物 ...'
if ($SkipCheck) {
    Write-Host '已跳过（-SkipCheck）。'
}
else {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        $python = Get-Command py -ErrorAction SilentlyContinue
    }
    if ($python) {
        & $python.Source (Join-Path $repoRoot 'tools\check_exe.py') $exePath
        if ($LASTEXITCODE -ne 0) {
            throw '产物自检未通过，说明 exe 仍然依赖外部运行库，不要就这样发出去。'
        }
    }
    else {
        Write-Warning '没有找到 python，跳过产物自检。'
    }
}

Write-Host ''
Write-Host '[4/4] 复制到 dist ...'
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$distPath = Join-Path $distDir 'Entray.exe'
Copy-Item -LiteralPath $exePath -Destination $distPath -Force

$sizeKib = [math]::Round((Get-Item -LiteralPath $distPath).Length / 1KB, 1)
Write-Host ''
Write-Host "完成：$distPath（$sizeKib KiB）" -ForegroundColor Green
Write-Host '这个文件可以直接拷到任何 Windows 电脑上双击运行，不需要安装任何东西。'
