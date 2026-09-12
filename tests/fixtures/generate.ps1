# 生成 tests/fixtures 下的测试订阅夹具（可重复运行，结果应稳定）
#
# 注意：PowerShell 中 `,` 的优先级高于 `+`，所以 @() 里每个元素必须加括号，
# 否则 'a' + 'b', 'c' + 'd' 会被拼成单个字符串。
$ErrorActionPreference = 'Stop'
$dir = $PSScriptRoot

function B64([string]$s) { [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s)) }
function B64U([string]$s) {
  ([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s))).TrimEnd('=').Replace('+', '-').Replace('/', '_')
}

$uuid = 'b831381d-6324-4d53-ad4f-8cda48b30811'

# --- VMess（v2rayN 的 base64(JSON) 格式）---------------------------------
$vmessWs = '{"v":"2","ps":"VMess-WS-TLS","add":"vm.example.com","port":"443","id":"' + $uuid + '","aid":"0","scy":"auto","net":"ws","type":"none","host":"vm.example.com","path":"/vmws","tls":"tls","sni":"vm.example.com","alpn":"h2,http/1.1","fp":"chrome"}'
$vmessGrpc = '{"v":"2","ps":"VMess-gRPC","add":"vm2.example.com","port":8443,"id":"' + $uuid + '","aid":0,"scy":"aes-128-gcm","net":"grpc","type":"none","path":"grpcsvc","tls":"tls","sni":"vm2.example.com"}'
$vmessTcp = '{"v":"2","ps":"VMess-TCP","add":"vm3.example.com","port":8080,"id":"' + $uuid + '","aid":64,"scy":"auto","net":"tcp","type":"none","tls":""}'

# --- SSR ------------------------------------------------------------------
$ssrInner = 'ssr.example.com:8388:auth_aes128_md5:aes-256-cfb:http_simple:' + (B64U 'ssrpass') +
            '/?obfsparam=' + (B64U 'www.bing.com') + '&protoparam=' + (B64U '12345:abc') +
            '&remarks=' + (B64U 'SSR-Node') + '&group=' + (B64U 'test-group')

$uris = @(
  # Shadowsocks
  ('ss://' + (B64 'aes-256-gcm:sspass') + '@ss.example.com:8443#SS-Node'),
  ('ss://' + (B64 'chacha20-ietf-poly1305:pw2') + '@ss2.example.com:443/?plugin=obfs-local%3Bobfs%3Dhttp%3Bobfs-host%3Dwww.bing.com#SS-OBFS'),
  ('ss://' + (B64 'aes-128-gcm:pw3@ss3.example.com:8388') + '#SS-Legacy'),

  # ShadowsocksR
  ('ssr://' + (B64U $ssrInner)),

  # VMess
  ('vmess://' + (B64 $vmessWs)),
  ('vmess://' + (B64 $vmessGrpc)),
  ('vmess://' + (B64 $vmessTcp)),

  # VLESS
  ('vless://' + $uuid + '@vl.example.com:443?encryption=none&security=tls&sni=vl.example.com&type=ws&host=vl.example.com&path=%2Fvlws&fp=chrome&alpn=h2%2Chttp%2F1.1#VLESS-WS-TLS'),
  ('vless://' + $uuid + '@vl2.example.com:443?encryption=none&security=reality&sni=www.microsoft.com&fp=chrome&pbk=YQfPqk3nJP8vT1sBcDeFgHiJkLmNoPqRsTuVwXyZ012&sid=0123abcd&type=tcp&flow=xtls-rprx-vision#VLESS-REALITY'),
  ('vless://' + $uuid + '@vl3.example.com:8443?encryption=none&security=tls&sni=vl3.example.com&type=grpc&serviceName=vlgrpc&fp=firefox#VLESS-gRPC'),

  # Trojan
  ('trojan://tjpw@tj.example.com:443?sni=tj.example.com&type=ws&path=%2Ftjws&host=tj.example.com#TROJAN-WS'),
  ('trojan://tjpw2@tj2.example.com:443?sni=tj2.example.com&type=grpc&serviceName=tjgrpc#TROJAN-gRPC'),

  # Hysteria v1 / v2
  ('hysteria://hy.example.com:443?auth=hypw&peer=hy.example.com&insecure=1&upmbps=100&downmbps=200&alpn=h3&protocol=udp#HYSTERIA1'),
  ('hysteria2://hy2pw@hy2.example.com:443?sni=hy2.example.com&insecure=1&obfs=salamander&obfs-password=obfspw&up=100&down=200#HYSTERIA2'),

  # TUIC
  ('tuic://' + $uuid + ':tuicpw@tuic.example.com:443?congestion_control=bbr&alpn=h3&sni=tuic.example.com&udp_relay_mode=native&allow_insecure=1#TUIC'),

  # Snell
  ('snell://snellpsk@snell.example.com:443?version=4&obfs=http&obfs-host=www.bing.com#SNELL'),

  # 通用
  ('socks5://user:pa%40ss@127.0.0.1:1080#Local-SOCKS'),
  ('https://1.2.3.4:443#TLS-HTTP-Proxy')
)

$plain = ($uris -join "`n") + "`n"
[IO.File]::WriteAllText((Join-Path $dir 'all_protocols.txt'), $plain, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $dir 'all_protocols_b64.txt'), (B64 $plain), [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $dir 'sample_plain.txt'), $plain, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $dir 'sample_base64.txt'), (B64 $plain), [Text.UTF8Encoding]::new($false))

Write-Host ("已生成 {0} 条链接 -> all_protocols.txt / all_protocols_b64.txt" -f $uris.Count)
