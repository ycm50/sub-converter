# 配置校验脚本：转换订阅 → 用真实客户端内核校验产物
#
#   .\tools\validate.ps1 -Setup                                  # 下载三个内核
#   .\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target clash
#   .\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target xray
#   .\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target singbox
#   .\tools\validate.ps1 -Source sub.yaml -Target xray -ProbeCert  # 顺带探测证书指纹
#
# 注意：参数名不能用 -Input —— $Input 是 PowerShell 自动变量，会把同名参数覆盖掉。
#
# 内核版本（用 -Setup 自动下载到 tools\bin\，该目录不入库）：
#   mihomo   v1.19.30   MetaCubeX/mihomo
#   Xray     v26.9.9    XTLS/Xray-core
#   sing-box v1.14.0    SagerNet/sing-box
[CmdletBinding()]
param(
  [Alias("Src")]
  [string]$Source,
  [ValidateSet("clash", "xray", "singbox")]
  [string]$Target = "clash",
  [switch]$Setup,
  # 转换时加 --probe-cert：主动连节点取证书 SHA256，写进 xray 的 pinnedPeerCertSha256。
  # 不加的话，「证书与 SNI 对不上」的机场节点在 Xray 下必然握手失败（客户端全是 -1）。
  [switch]$ProbeCert
)

$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false) } catch { }

$root = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $PSScriptRoot "bin"
$subconv = Join-Path $root "build\subconv.exe"

# curl：优先用环境变量指定的，其次本机 MSYS2 的两套安装位置，最后退回 PATH 上的 curl
# （Windows 10/11 自带 C:\Windows\System32\curl.exe —— CI 里走的就是它）。
function Resolve-Curl {
  if ($env:SUBCONV_CURL -and (Test-Path $env:SUBCONV_CURL)) { return $env:SUBCONV_CURL }
  foreach ($candidate in "A:\msys64\usr\bin\curl.exe", "C:\msys64\usr\bin\curl.exe") {
    if (Test-Path $candidate) { return $candidate }
  }
  $onPath = Get-Command curl.exe -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }
  throw "找不到 curl.exe：请装 MSYS2，或用 SUBCONV_CURL 指定路径"
}

# 下载内核 / geodata 都要连 GitHub。开发机常常需要走本地代理，CI 则是直连：
# 有 SUBCONV_VALIDATE_PROXY 就用它，否则只在 127.0.0.1:10808 真的在监听时才走代理。
function Test-LocalPort([int]$Port) {
  $client = $null
  try {
    $client = New-Object System.Net.Sockets.TcpClient
    $task = $client.ConnectAsync("127.0.0.1", $Port)
    if ($task.Wait(300) -and $client.Connected) { return $true }
    return $false
  } catch {
    return $false
  } finally {
    if ($client) { $client.Close() }
  }
}

$curl = Resolve-Curl
$proxy = $env:SUBCONV_VALIDATE_PROXY
if (-not $proxy -and (Test-LocalPort 10808)) { $proxy = "http://127.0.0.1:10808" }
if ($proxy) {
  $env:http_proxy = $proxy
  $env:https_proxy = $proxy
  Write-Host "下载内核走代理：$proxy" -ForegroundColor DarkGray
} else {
  Remove-Item Env:http_proxy, Env:https_proxy -ErrorAction SilentlyContinue
}

# 原生命令包装器
#
# Windows PowerShell 5.1 会把原生命令写到 stderr 的每一行包成 ErrorRecord，而本脚本设了
# $ErrorActionPreference = "Stop"，于是内核（mihomo/xray）与 subconv -v 的正常日志会被当成
# 终止性错误，脚本在"转换失败"的假象下中断（外部用 2>&1 捕获输出时必现）。
# 这里临时放宽错误首选项，把 stdout/stderr 统一按文本转发，再返回真实退出码。
function Invoke-Native {
  param(
    [Parameter(Mandatory)][string]$Exe,
    [string[]]$Arguments = @(),
    [string]$WorkDir,
    [string[]]$Mute = @()
  )
  $prev = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  $code = 0
  $lines = @()
  try {
    if ($WorkDir) { Push-Location $WorkDir }
    try {
      $lines = & $Exe @Arguments 2>&1
      $code = $LASTEXITCODE
    } finally {
      if ($WorkDir) { Pop-Location }
    }
  } finally {
    $ErrorActionPreference = $prev
  }
  foreach ($line in @($lines)) {
    $text = [string]$line
    $skip = $false
    foreach ($p in $Mute) { if ($text -match $p) { $skip = $true; break } }
    if (-not $skip) { Write-Host $text }
  }
  return $code
}

function Get-Tool([string]$name, [string]$url) {
  $dir = Join-Path $binDir $name
  $found = Get-ChildItem $dir -Recurse -Filter "*.exe" -ErrorAction SilentlyContinue |
           Select-Object -First 1
  if ($found) { return $found.FullName }
  Write-Host "下载 $name ..." -ForegroundColor Yellow
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  $zip = Join-Path $dir "pkg.zip"
  $code = Invoke-Native $curl @("-sL", "-m", "900", "-o", $zip, $url)
  if ($code -ne 0) { throw "下载 $name 失败（curl exit $code）: $url" }
  Expand-Archive -Path $zip -DestinationPath $dir -Force
  Remove-Item $zip -Force
  return (Get-ChildItem $dir -Recurse -Filter "*.exe" | Select-Object -First 1).FullName
}

function Get-Mihomo  { Get-Tool "mihomo" "https://github.com/MetaCubeX/mihomo/releases/download/v1.19.30/mihomo-windows-amd64-compatible-v1.19.30.zip" }
function Get-Xray    { Get-Tool "xray" "https://github.com/XTLS/Xray-core/releases/download/v26.9.9/Xray-windows-64.zip" }
function Get-Singbox { Get-Tool "singbox" "https://github.com/SagerNet/sing-box/releases/download/v1.14.0/sing-box-1.14.0-windows-amd64.zip" }

function Test-GeodataFile([string]$path, [int]$minBytes) {
  if (-not (Test-Path $path)) { return $false }
  if ((Get-Item $path).Length -lt $minBytes) { return $false }
  # curl 不加 --fail 时会把 GitHub 的错误页/限流页原样写进目标文件，
  # mihomo 拿到后只会报 "GeoSite.dat invalid" 并把它删掉（这个坑踩过一次）
  $head = [System.IO.File]::ReadAllBytes($path)[0..15]
  $text = -join ($head | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '.' } })
  if ($text -match 'html|DOCTYPE|version https') { return $false }
  return $true
}

function Get-Geodata([string]$work) {
  # 两个文件分别判，别因为 geoip 在就跳过 geosite（否则坏文件永远修不回来）
  $min = 1MB
  foreach ($f in "geoip.metadb", "geosite.dat") {
    $dest = Join-Path $work $f
    if (Test-GeodataFile $dest $min) { continue }
    if (Test-Path $dest) {
      Write-Host "geodata $f 无效，重新获取" -ForegroundColor Yellow
      Remove-Item $dest -Force
    }
    Write-Host "下载 geodata $f ..." -ForegroundColor Yellow
    $code = Invoke-Native $curl @("-fsSL", "-m", "300", "--connect-timeout", "15", "-o", $dest,
      "https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/$f")
    if (($code -eq 0) -and (Test-GeodataFile $dest $min)) { continue }
    if (Test-Path $dest) { Remove-Item $dest -Force }

    # 回退：xray 工具包里那份 v2fly 格式的 geosite.dat，mihomo 一样能加载
    # （实测 GEOSITE,cn 命中 111167 条记录）。只有已经下过 xray 时才可用。
    $alt = Join-Path $binDir "xray\$f"
    if (($f -eq "geosite.dat") -and (Test-GeodataFile $alt $min)) {
      Copy-Item $alt $dest -Force
      Write-Host "  -> 下载不可用，改用 $alt" -ForegroundColor Yellow
      continue
    }
    throw "获取 geodata $f 失败（curl exit $code）：规则里的 GEOSITE/GEOIP 无法校验"
  }
}

if ($Setup) {
  Write-Host ("mihomo   -> " + (Get-Mihomo))  -ForegroundColor Green
  Write-Host ("xray     -> " + (Get-Xray))    -ForegroundColor Green
  Write-Host ("sing-box -> " + (Get-Singbox)) -ForegroundColor Green
  exit 0
}

if (-not $Source) { throw "请用 -Source 指定订阅文件" }
if (-not (Test-Path $Source)) { throw "订阅文件不存在: $Source" }
if (-not (Test-Path $subconv)) { throw "未找到 $subconv，请先运行 build.ps1" }

$work = Join-Path $binDir "work-$Target"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$ext = if ($Target -eq "clash") { "yaml" } else { "json" }
$config = Join-Path $work "config.$ext"

Write-Host "== 转换 ($Target) ==" -ForegroundColor Cyan
$convertArgs = @("-i", $Source, "-t", $Target, "-o", $config, "-v")
if ($ProbeCert) { $convertArgs += "--probe-cert" }
$code = Invoke-Native $subconv $convertArgs
if ($code -ne 0) { throw "转换失败" }

switch ($Target) {
  "clash" {
    $exe = Get-Mihomo
    Get-Geodata $work
    Write-Host "== mihomo 校验 ==" -ForegroundColor Cyan
    $code = Invoke-Native $exe @("-t", "-d", $work, "-f", $config)
    if ($code -ne 0) { throw "mihomo 校验失败" }
  }
  "xray" {
    $exe = Get-Xray
    Write-Host "== xray 校验 ==" -ForegroundColor Cyan
    $code = Invoke-Native $exe @("run", "-test", "-c", (Split-Path $config -Leaf)) `
      -WorkDir $work -Mute '\[Warning\]'
    if ($code -ne 0) { throw "xray 校验失败" }
  }
  "singbox" {
    $exe = Get-Singbox
    Write-Host "== sing-box 校验 ==" -ForegroundColor Cyan
    $code = Invoke-Native $exe @("check", "-c", $config)
    if ($code -ne 0) { throw "sing-box 校验失败" }
  }
}

Write-Host "校验通过: $config" -ForegroundColor Green
