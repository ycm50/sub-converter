# subconv —— 校验 v2rayn 目标输出能否被 v2rayN / v2rayNG 真正导入
#
# 不依赖任何客户端本体：用 .NET 原样重放两端的导入逻辑。
#
#   v2rayN  ServiceLib/Handler/Fmt/InnerFmt.cs
#     ResolveSingle:  Uri.AbsolutePath.TrimStart('/')
#                     -> Utils.Base64Decode（'_'->'/'，'-'->'+'，补 '='）
#                     -> ProfileItem
#                     硬性要求 ConfigVersion == 4；ConfigType 必须是已定义枚举；
#                     CoreType 只允许 null / Xray(2) / sing_box(24)。
#     ToUriSingle 的产出形如 v2rayn://<ConfigType 小写>/<url-safe base64>
#
#   v2rayNG fmt/V2rayNFmt.kt
#     parseShareItem: Utils.decode(str.substringAfterLast('/')) -> V2rayNShareItem
#                     -> toProfileItem()，其中 10 -> EConfigType.HTTP
#                     IndexId 走 putIfAbsent，重名会被静默丢弃
#
# 用法：
#   .\tools\verify-v2rayn.ps1 -Path out-v2rayn.txt
#   .\tools\verify-v2rayn.ps1 -Path out-v2rayn.txt -LegacyNoSegment
#     -LegacyNoSegment 会额外演示：省掉路径段的旧写法在 v2rayN 侧全部无法解析。

param(
  [Parameter(Mandatory = $true)][string]$Path,
  [switch]$LegacyNoSegment
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) {
  Write-Host "找不到文件：$Path" -ForegroundColor Red
  exit 2
}

# v2rayN EConfigType 的已定义取值（Enum.IsDefined）
$definedConfigTypes = @{
  1 = 'VMess'; 2 = 'Custom'; 3 = 'Shadowsocks'; 4 = 'SOCKS'; 5 = 'VLESS'
  6 = 'Trojan'; 7 = 'Hysteria2'; 8 = 'TUIC'; 9 = 'WireGuard'; 10 = 'HTTP'
  11 = 'Anytls'; 12 = 'Naive'; 13 = 'Outbound'; 101 = 'PolicyGroup'; 102 = 'ProxyChain'
}
# InnerFmt.ResolveSingle 允许的 CoreType
$allowedCoreTypes = @(2, 24)

function ConvertFrom-V2rayNBase64([string]$text) {
  if ([string]::IsNullOrEmpty($text)) { return '' }
  $t = $text.Trim() -replace "[`r`n]", '' -replace '_', '/' -replace '-', '+' -replace ' ', ''
  if ($t.Length % 4 -gt 0) { $t = $t.PadRight($t.Length + 4 - ($t.Length % 4), '=') }
  return [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($t))
}

function ConvertFrom-V2rayNGBase64([string]$text) {
  # Utils.decode：先按标准表解，再去掉 '=' 重试，最后按 URL-safe 表解
  foreach ($t in @($text, $text.TrimEnd('='))) {
    foreach ($urlSafe in @($false, $true)) {
      try {
        $s = if ($urlSafe) { $t -replace '-', '+' -replace '_', '/' } else { $t }
        if ($s.Length % 4 -gt 0) { $s = $s.PadRight($s.Length + 4 - ($s.Length % 4), '=') }
        return [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($s))
      } catch { }
    }
  }
  return ''
}

function Get-Segment([string]$line) {
  # v2rayn://<segment>/<payload>  ->  segment
  $rest = $line.Substring(9)
  $slash = $rest.IndexOf('/')
  if ($slash -lt 0) { return $null }
  return $rest.Substring(0, $slash)
}

$rawLines = Get-Content -LiteralPath $Path
$lines = @($rawLines | Where-Object { $_.Trim() -ne '' })

$v2rayNOk = 0
$v2rayNGFail = 0
$problems = New-Object System.Collections.Generic.List[string]
$seenIndexId = @{}
$duplicateIndexId = 0
$typeHistogram = @{}
$coreTypeSeen = @{}

foreach ($line in $lines) {
  $short = $line.Substring(0, [Math]::Min(60, $line.Length))

  if (-not $line.StartsWith('v2rayn://')) {
    $problems.Add("不是 v2rayn:// 行：$short")
    $v2rayNGFail++
    continue
  }
  $segment = Get-Segment $line

  # ---------------- v2rayN: InnerFmt.ResolveSingle ----------------
  $v2rayNError = $null
  try {
    $uri = [System.Uri]::new($line)
    $payloadFromPath = $uri.AbsolutePath.TrimStart('/')
    $jsonText = ConvertFrom-V2rayNBase64 $payloadFromPath
  } catch {
    $jsonText = ''
  }
  if ([string]::IsNullOrWhiteSpace($jsonText)) {
    $v2rayNError = 'AbsolutePath 取不到载荷（Uri 把载荷当成了 Host）'
  } else {
    $obj = $null
    try { $obj = $jsonText | ConvertFrom-Json } catch { $v2rayNError = "载荷不是合法 JSON：$($_.Exception.Message)" }
    if (-not $v2rayNError) {
      if ($obj.ConfigVersion -ne 4) {
        $v2rayNError = "ConfigVersion=$($obj.ConfigVersion)，InnerFmt 只接受 4"
      } elseif (-not $definedConfigTypes.ContainsKey([int]$obj.ConfigType)) {
        $v2rayNError = "ConfigType=$($obj.ConfigType) 不是已定义的 EConfigType"
      } elseif ($null -ne $obj.PSObject.Properties['CoreType'] -and
                $null -ne $obj.CoreType -and $allowedCoreTypes -notcontains [int]$obj.CoreType) {
        $v2rayNError = "CoreType=$($obj.CoreType) 不在 {null, Xray(2), sing_box(24)} 内"
      } elseif ([string]::IsNullOrEmpty($obj.Address) -or [int]$obj.Port -lt 1 -or [int]$obj.Port -gt 65535) {
        $v2rayNError = "Address/Port 非法：$($obj.Address):$($obj.Port)"
      }
    }
  }
  if ($v2rayNError) {
    $problems.Add("v2rayN 会丢弃：$v2rayNError（$short…）")
  } else {
    $v2rayNOk++
  }

  # ---------------- v2rayNG: V2rayNFmt.parseShareItem ----------------
  $lastSlash = $line.LastIndexOf('/')
  $payloadNG = if ($lastSlash -ge 0) { $line.Substring($lastSlash + 1) } else { '' }
  $jsonNG = ConvertFrom-V2rayNGBase64 $payloadNG
  if ([string]::IsNullOrWhiteSpace($jsonNG)) {
    $problems.Add("v2rayNG 解不出载荷：$short…")
    $v2rayNGFail++
  } else {
    $item = $null
    try { $item = $jsonNG | ConvertFrom-Json } catch { }
    if ($null -eq $item) {
      $problems.Add("v2rayNG 载荷不是合法 JSON：$short…")
      $v2rayNGFail++
    } else {
      $expectedSegment = ([string]$definedConfigTypes[[int]$item.ConfigType]).ToLower()
      if ($null -ne $segment -and $segment -ne $expectedSegment) {
        $problems.Add("路径段 '$segment' 与 ConfigType=$($item.ConfigType)（应为 '$expectedSegment'）不一致：$short…")
      }
      $key = [string]$item.IndexId
      if ($seenIndexId.ContainsKey($key)) {
        $duplicateIndexId++
        $problems.Add("IndexId 重复会被 v2rayNG 的 putIfAbsent 丢掉：'$key'")
      } else {
        $seenIndexId[$key] = $true
      }
      $ct = [int]$item.ConfigType
      if (-not $typeHistogram.ContainsKey($ct)) { $typeHistogram[$ct] = 0 }
      $typeHistogram[$ct]++
      if ($null -ne $item.PSObject.Properties['CoreType'] -and $null -ne $item.CoreType) {
        $coreTypeSeen[[int]$item.CoreType] = $true
      }
    }
  }
}

Write-Host "== v2rayn 输出校验：$Path" -ForegroundColor Cyan
Write-Host ("   行数        : {0}" -f $lines.Count)
Write-Host ("   v2rayN 可导入: {0}" -f $v2rayNOk)
Write-Host ("   v2rayNG 异常 : {0}" -f $v2rayNGFail)
Write-Host ("   IndexId 重复 : {0}" -f $duplicateIndexId)
Write-Host "   ConfigType 分布:"
foreach ($k in ($typeHistogram.Keys | Sort-Object)) {
  Write-Host ("      {0,3} {1,-12} x{2}" -f $k, $definedConfigTypes[$k], $typeHistogram[$k])
}
if ($coreTypeSeen.Count -gt 0) {
  Write-Host ("   CoreType 出现: {0}" -f (($coreTypeSeen.Keys | Sort-Object) -join ', '))
} else {
  Write-Host "   CoreType 出现: 无（合法）"
}

if ($LegacyNoSegment) {
  # 对照演示两个曾经踩过的坑各自会造成什么后果
  $noSegmentOk = 0
  $oldVersionOk = 0
  foreach ($line in $lines) {
    $rest = $line.Substring(9)
    $slash = $rest.IndexOf('/')
    if ($slash -lt 0) { continue }
    $payload = $rest.Substring($slash + 1)

    # A) 省掉 <协议>/ 路径段（旧写法 v2rayn://<载荷>）
    try {
      $uri = [System.Uri]::new('v2rayn://' + $payload)
      $jsonA = ConvertFrom-V2rayNBase64 $uri.AbsolutePath.TrimStart('/')
      if (-not [string]::IsNullOrWhiteSpace($jsonA)) { $noSegmentOk++ }
    } catch { }

    # B) 路径段保留，但把 ConfigVersion 按修复前的写法设成 2
    #    InnerFmt.ResolveSingle: profileItem.ConfigVersion != 4 -> return null
    try {
      $jsonB = ConvertFrom-V2rayNBase64 $payload
      $objB = $jsonB | ConvertFrom-Json
      $objB.ConfigVersion = 2
      if ([int]$objB.ConfigVersion -eq 4) { $oldVersionOk++ }
    } catch { }
  }
  Write-Host ""
  Write-Host "== 对照：两个已修的坑各自会造成什么后果" -ForegroundColor Yellow
  Write-Host ("   A) 省掉 <协议>/ 路径段              -> v2rayN 可解析 {0} / {1}" -f $noSegmentOk, $lines.Count) -ForegroundColor Yellow
  Write-Host ("   B) ConfigVersion 不是 4（曾写成 2） -> v2rayN 可解析 {0} / {1}" -f $oldVersionOk, $lines.Count) -ForegroundColor Yellow
}

if ($problems.Count -gt 0) {
  Write-Host ""
  Write-Host "== 问题明细" -ForegroundColor Red
  $problems | Select-Object -First 40 | ForEach-Object { Write-Host "   $_" -ForegroundColor Red }
  Write-Host ""
  exit 1
}

Write-Host ""
Write-Host "全部通过：v2rayN 与 v2rayNG 都能导入该文件。" -ForegroundColor Green
exit 0
