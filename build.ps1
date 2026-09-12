# 构建脚本（MSYS2 UCRT64 + Ninja）
#
#   .\build.ps1                 # Release 构建
#   .\build.ps1 -Test           # 构建并运行单元测试
#   .\build.ps1 -Clean          # 先清空 build 目录
[CmdletBinding()]
param(
  [string]$Config = "Release",
  [string]$BuildDir = "build",
  [switch]$Clean,
  [switch]$Test
)

$ErrorActionPreference = "Stop"

# 保证中文输出不乱码
try {
  [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
  $OutputEncoding = [Console]::OutputEncoding
} catch { }

$root = $PSScriptRoot
$build = Join-Path $root $BuildDir
$prefix = "A:/msys64/ucrt64"

if ($env:MSYSTEM_PREFIX) { $prefix = $env:MSYSTEM_PREFIX }
$env:PATH = "A:\msys64\ucrt64\bin;A:\msys64\usr\bin;$env:PATH"

# 仓库内所有 .ps1 必须带 UTF-8 BOM：无 BOM 时 Windows PowerShell 5.1 会按 ANSI(GBK) 解码中文，
# 尾字节可能吞掉引号/大括号，报出莫名其妙的"语法错误"。这类故障极难定位，所以直接前置拦截。
$bad = @()
$ps1 = Get-ChildItem -Path $root -Recurse -File -Filter *.ps1 -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -notmatch '\\(build|third_party|\.git|tools\\bin)\\' }
foreach ($f in $ps1) {
  $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
  if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
    $bad += $f.FullName
  }
}
if ($bad.Count -gt 0) {
  Write-Host "以下 .ps1 缺少 UTF-8 BOM，会导致 PowerShell 5.1 解析中文失败：" -ForegroundColor Red
  $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
  throw "编码检查未通过"
}

if ($Clean -and (Test-Path $build)) {
  Write-Host "清理 $build" -ForegroundColor Yellow
  Remove-Item -Recurse -Force $build
}

Write-Host "== configure ==" -ForegroundColor Cyan
& cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$prefix"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== build ==" -ForegroundColor Cyan
& cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $build "subconv.exe"
if (Test-Path $exe) {
  $size = (Get-Item $exe).Length
  Write-Host ("== 产物: {0} ({1:N0} 字节) ==" -f $exe, $size) -ForegroundColor Green
}

if ($Test) {
  $tests = Join-Path $build "subconv_tests.exe"
  Write-Host "== 单元测试 ==" -ForegroundColor Cyan
  & $tests
  exit $LASTEXITCODE
}

exit 0
