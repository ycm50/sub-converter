# subconv — 基于 C++ 的订阅转换工具

把机场订阅（`ss` / `ssr` / `vmess` / `vless` / `trojan` / `hysteria` / `hysteria2` / `tuic` /
`snell` / `socks5` / `http` 分享链接）转换成各客户端可直接加载的配置，并提供兼容
[subconverter](https://github.com/tindy2013/subconverter) 的 HTTP `/sub` 接口。

## 当前进度

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 工具链 + CMake 骨架 | ✅ |
| M1 | `ss`/`socks5`/`http` 解析 + Clash YAML 输出 | ✅ |
| M2 | 全协议解析 + **Xray** / **sing-box** 目标 | ✅ |
| M3 | libcurl 订阅抓取（代理/重定向/缓存/内容嗅探） | ✅ |
| M4 | 规则模板引擎 + 分组生成 + 过滤/重命名/emoji | 🚧 分组与**可挑选的分流规则集**已内建（见「[分流规则集](#分流规则集rules仅-clash-目标)」）；模板外置 `data/`、节点过滤/重命名待做 |
| M5 | HTTP `/sub` 服务 + **Web UI** | ✅ 见「[Web UI 与 HTTP 接口](#web-ui-与-http-接口)」 |
| M6 | golden file 测试 + 静态发布 + 文档 | ⏳ |

> **当前状态**：M0–M3、M5 已完成，M4 进行中。
> Clash 目标已生成 3 个 emoji 分组（🚀 节点选择 / ♻️ 自动选择 / 🐟 漏网之鱼，`--no-emoji` 可关闭）
> 与可挑选的 rules（默认 5 条：`GEOIP,LAN` / `GEOIP,private` / `GEOSITE,cn` / `GEOIP,CN` / `MATCH`，
> 用 `--rulesets` 或 Web UI 的复选增减）；
> 另有可挑选的 `dns.nameserver`（默认 `cloudflare,google`，**不再是原来的 223.5.5.5**）与
> `ipv6`（默认 **开**），见「[DNS 与 IPv6](#dnsdnsnameserver与-ipv6)」；还支持设置订阅名称，
> 见「[订阅名称](#订阅名称)」。
> Xray / sing-box 目标只含入站出站结构，不含分组、规则与 DNS。另有 `links` / `base64` 两个目标，
> 导出 v2rayNG / v2rayN / Shadowrocket 通用的分享链接（详见下方「[分享链接输出](#分享链接输出-v2rayng)」）。
> 所有目标的产物都已用真实内核校验通过。`data/` 目前只有 `web/`（Web UI 源文件）与说明文档；
> 分流规则集与 DNS 预设刻意留在代码里（`src/emit/rulesets.cpp`、`src/emit/dns.cpp`），原因见 `data/README.md`。

**目标优先级**：Clash(mihomo) → Xray → sing-box。

## 协议 × 目标 支持矩阵

各内核能力边界不同，subconv 会**按目标自动裁剪并给出告警**，而不是产出无法加载的配置。

| 协议 | clash (mihomo) | xray | sing-box | v2rayNG |
|---|---|---|---|---|
| shadowsocks | ✅ 含 obfs / v2ray-plugin | ⚠️ 仅 AEAD / 2022 系列，旧加密自动跳过 | ✅ 含插件 | ✅ 插件以 `plugin=` 回写 |
| shadowsocksr | ✅ | ❌ 不支持 | ❌ 不支持 | ❌ 不支持 |
| vmess | ✅ ws/grpc/h2/http | ✅ | ✅ | ✅ |
| vless + reality | ✅ | ✅ | ✅ | ✅ |
| trojan | ✅ | ✅ | ✅ | ✅ |
| hysteria v1 | ✅ | ❌ 不支持 | ✅ | ❌ 只支持 hysteria2 |
| hysteria2 | ✅ | ❌ 不支持 | ✅ | ✅ |
| tuic | ✅ | ❌ 不支持 | ✅ | ❌ 未启用 |
| snell | ✅ | ❌ 不支持 | ❌ 不支持 | ❌ 不支持 |
| socks5 / http | ✅ | ✅ | ✅ | ✅ socks5 / ⚠️ http 需用 `-t v2rayn` |
| wireguard | ⏳ 未实现 | ⏳ | ⏳ | ⏳ |

## 快速开始

```powershell
# 1. 构建（Release + 单元测试）
.\build.ps1 -Test

# 2. 生成测试夹具（可选，覆盖 18 节点 / 11 种协议）
.\tests\fixtures\generate.ps1

# 3. 转换：本地文件 / URL / Clash YAML 都可以作为输入
.\build\subconv.exe -i tests\fixtures\all_protocols_b64.txt -t clash   -o out.yaml -v
.\build\subconv.exe -i https://example.com/sub -t clash -o out.yaml --proxy socks5://127.0.0.1:10808
.\build\subconv.exe -i upstream-clash-config.yaml -t xray -o out.json

# 4. 用真实内核校验产物
.\tools\validate.ps1 -Setup                                             # 首次下载三个内核
.\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target clash
.\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target xray
.\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target singbox

# 5. 图形界面 + HTTP 接口
.\build\subconv.exe serve --open                                         # http://127.0.0.1:25500/

# 6. 导出 v2rayNG 能导入的节点链接（或订阅）
.\build\subconv.exe -i upstream.yaml -t links  -o nodes.txt              # 每行一条，剪贴板导入用
.\build\subconv.exe -i upstream.yaml -t base64 -o subscription.txt       # base64，填进「订阅设置」
.\build\subconv.exe -i upstream.yaml -t v2rayn -o full.txt               # v2rayN 格式，含 http 节点
```

CLI 选项：

```
输入
  -i, --input <路径|URL>   输入订阅，可重复（多个输入会合并）
                           支持：分享链接列表（含 Base64 包裹）、Clash YAML
输出
  -t, --target <名称>      目标：clash | xray | singbox | links | base64 | v2rayn，默认 clash
  -o, --output <路径>      输出文件，默认写到 stdout
      --no-emoji           分组名不加 emoji
      --no-udp             节点关闭 UDP
      --no-dedup           不做去重
      --tfo                开启 TCP Fast Open
      --sort               按节点名排序
      --clash-legacy       输出原版 Clash 兼容语法（剔除 meta 独有协议）
      --no-rules           不输出 rules 段
      --rulesets <列表>    分流规则集，逗号分隔，默认 local,cn；见 --list-rulesets
      --list-rulesets      列出全部可选规则集
      --dns <列表>         dns.nameserver：预设 id 或字面地址（IP / DoH URL），逗号分隔
                           默认 cloudflare,google；见 --list-dns；给空串则不写 nameserver
      --ipv6              开启 ipv6（根节点 + dns 段 + 各 DNS 预设的 IPv6 地址），默认开
      --no-ipv6           关闭 ipv6
      --name <订阅名>      写进配置首行注释，服务端还会用它当下载文件名
      --list-dns           列出全部 DNS 预设
抓取
      --proxy <URL>        http:// 或 socks5:// 代理
      --ua <字符串>        User-Agent，默认 clash-verge/v2.0.0
      --timeout <秒>       单次请求超时，默认 20
      --retries <次数>     失败重试次数，默认 2
  -k, --insecure           跳过 TLS 证书校验
      --header <K: V>      附加请求头，可重复
      --cache-dir <目录>   缓存目录，默认系统临时目录下 subconv-cache
      --cache-ttl <秒>     缓存有效期，默认 300；0 表示永不过期
      --no-cache           禁用缓存
服务
  serve                    启动 HTTP 服务 + Web UI
      --listen <地址>      监听地址，默认 127.0.0.1（0.0.0.0 会暴露到局域网）
      --port <端口>        监听端口，默认 25500；0 表示自动分配
      --open               启动后自动打开浏览器
                           抓取类选项会作为服务端默认值生效
其他
  -v, --verbose            打印解析/抓取明细
      --list-targets       列出输出目标
```

目标别名：`clash` / `clash.meta` / `meta` / `mihomo` / `clashplus` 指向同一目标；
`v2ray` / `xray-json` 指向 xray；`sing-box` / `sb` 指向 singbox；
`links` / `sharelinks` / `v2rayng-links` 指向分享链接列表；
`base64` / `v2rayng` / `v2rayng-sub` / `v2ray-sub` / `sssub` 指向 base64 订阅；
`v2rayn` / `v2rayn-share` / `v2rayn-full` 指向 v2rayN 分享格式。

## 分享链接输出（v2rayNG）

`links` 与 `base64` 两个目标把节点反向导出成**分享链接**，供 v2rayNG / v2rayN / Shadowrocket
等客户端导入（v2rayNG 的订阅内容就是「链接列表的 base64」）：

```powershell
.\build\subconv.exe -i upstream.yaml -t links -o nodes.txt        # 每行一条链接
.\build\subconv.exe -i upstream.yaml -t base64                     # 直接给客户端当订阅内容
```

格式对齐 v2rayNG 自身的实现（`com.v2ray.ang.fmt.*Fmt`，逐字段核对过源码）：

| 协议 | 产出形态 |
|---|---|
| shadowsocks | `ss://base64url(method:password)@host:port?plugin=...#名称`（SIP002；插件串原样回写） |
| vmess | `vmess://base64(JSON)`，18 个键、值全为字符串、键序固定 |
| vless | `vless://uuid@host:port?encryption=none&security=tls\|reality&…`（reality 带 `pbk`/`sid`/`spx`） |
| trojan | `trojan://password@host:port?security=tls&sni=…&type=…` |
| hysteria2 | `hysteria2://password@host:port?sni=…&obfs=salamander&obfs-password=…#名称` |
| socks5 | `socks://base64url(user:password)@host:port#名称` |

被跳过的协议会汇总成告警（CLI 打到 stderr，Web UI 显示在结果上方）：v2rayNG 是 Xray/v2fly
客户端，**不支持 ssr、snell、hysteria v1、tuic**。

### http(s) 代理怎么导进 v2rayN / v2rayNG：`v2rayn` 目标

标准分享链接**没有 http 代理的形态**——查 2dust/v2rayN 的 `FmtHandler.ResolveConfig` 和
2dust/v2rayNG 的 `AngConfigManager.configFmtParsers`，两边都**没有注册 `http://` 解析器**
（v2rayNG 的表里只有 vmess / ss / socks / socks4 / socks5 / trojan / vless / wireguard / hysteria2）。
所以带 http 节点的订阅走 `links` / `base64` 会少一截，这种情况用 `v2rayn` 目标：

```powershell
.\build\subconv.exe -i upstream.yaml -t v2rayn -o full.txt   # 每行一条 v2rayn://<协议>/<base64url(JSON)>
```

同一份 19 节点订阅（16 http + 2 ss + 1 vmess）能得到 **19 条**而不是 3 条。代价是只有
v2rayN / v2rayNG 认识这个格式（v2rayNG 的 `parseBatchConfig` 会把 `v2rayn://` 开头的行单独交给
`V2rayNFmt.parse`；v2rayN 的 `AddBatchServers4InnerUri` 里有 `EConfigType.HTTP => AddHttpServer`），
其它客户端请继续用 `links` / `base64`。

格式按 v2rayN 的 `ServiceLib/Handler/Fmt/InnerFmt.cs` 对齐（依据写在 `src/emit/sharelink.cpp` 注释里）：

```
v2rayn://{ConfigType.ToString().ToLower()}/{url-safe base64(PascalCase JSON)}
```

几个硬性要求：

- **路径段不能省**。v2rayN 的 `ResolveSingle` 取 `Uri.AbsolutePath.TrimStart('/')`；只写
  `v2rayn://<载荷>` 时 Uri 会把载荷当成 **Host**，`AbsolutePath` 退化成 `/`，解码出空串后
  **整条丢弃**（实测 19 条全废）。v2rayNG 用的是 `substringAfterLast('/')`，两种写法都认，
  所以统一按 v2rayN 的写法输出：`http` / `shadowsocks` / `vmess` / `vless` / `trojan` /
  `socks` / `hysteria2`（注意是枚举名小写，Shadowsocks **不是** `ss`）。
- **`ConfigVersion` 必须是 `4`**：`ResolveSingle` 里 `!= 4` 直接 `return null`。
- `CoreType` 只能是 `null` / `Xray(2)` / `sing_box(24)`；这里不写，合法。
- **`ConfigType` 用 v2rayN 的 `EConfigType` 编号**：1 VMess、3 Shadowsocks、4 SOCKS、5 VLESS、
  6 Trojan、7 Hysteria2、8 TUIC、9 WireGuard、**10 HTTP**。注意 v2rayNG 自己的 `EConfigType`
  把 WireGuard / Hysteria2 的编号对调了（WireGuard=7、Hysteria2=9），但它的
  `V2rayNShareItem.toProfileItem()` 是按 **v2rayN** 的编号映射的（`7 -> HYSTERIA2`、
  `9 -> WIREGUARD`、`10 -> HTTP`），所以以 v2rayN 的表为准。
- 载荷必须 **URL-safe base64**，否则标准 base64 里的 `/` 会被 `substringAfterLast('/')` 截断。
- `IndexId` 必须唯一（v2rayNG 用 `putIfAbsent` 去重，重名会**静默丢节点**；这里用节点名，
  `prepare_nodes` 已保证唯一）。
- `AllowInsecure` 两端都是字符串 `"true"` / `"false"`。

### http 节点的 TLS：只有 v2rayN 能生效

mihomo 里 `type: http` + `tls: true` 表示 **https 代理**，两端行为不同：

| 客户端 | http 出站的 TLS |
|---|---|
| v2rayN | ✅ `V2rayOutboundService` 对**所有**协议统一调 `FillBoundStreamSettings`，`StreamSecurity=tls` 会落到 http 出站的 `streamSettings`；Xray 官方文档（`config/outbounds/http.md`）也写明 http 出站的 `security` / `tlsSettings` 生效 |
| v2rayNG | ❌ `CoreOutboundBuilder` 里每个协议都调 `populateTlsSettings`，唯独 `toOutboundHttp` / `toOutboundSocks` 不调 → 导入后只能是**明文** http 代理，连不上 https 端口 |

所以带 TLS 的 http 节点在 `v2rayn` 输出里会**额外给一条告警**（不静默降级），这些节点请用
v2rayN（Xray 内核）导入。`clash` / `singbox` / `xray` 目标不受影响：mihomo、Xray 都支持 http
出站的 TLS，sing-box 的 `tls` 字段也经 `sing-box check` 严格校验通过（sing-box 对未知字段直接
报错，能通过即证明字段真实存在）。

链接的正确性靠**往返测试**保证：生成链接 → 用自己的解析器读回来 → 逐字段比对
（`tests/test_main.cpp` 的 `test_share_links` / `test_v2rayn_share`，覆盖 8 种协议、被跳过的 4~5 种
情况，以及 `v2rayn` 项的路径段、`ConfigVersion`、JSON 字段与控制字段类型）。

`v2rayn` 没法用自家解析器往返（项目里没有 `v2rayn://` **输入**解析器），另有
`tools/verify-v2rayn.ps1`：它用 .NET 原样重放 v2rayN 的 `AbsolutePath.TrimStart('/')` +
`Base64Decode`（`_`→`/`、`-`→`+`、补 `=`）+ `ConfigVersion == 4` + `Enum.IsDefined` 判定，
以及 v2rayNG 的 `substringAfterLast('/')` + `IndexId` 去重路径，逐条报告两端能否导入：

```powershell
.\tools\verify-v2rayn.ps1 -Path full.txt -LegacyNoSegment   # 末尾会把旧写法的报废率也打出来
```

## 分流规则集（`rules`，仅 Clash 目标）

勾上「输出 rules」时，可以再挑具体要哪些**规则集**（界面里是一排复选，默认勾「直连本地 + 中国直连」）。
命令行等价：

```powershell
.\build\subconv.exe --list-rulesets                        # 看全部可选规则集
.\build\subconv.exe -i up.yaml -t clash --rulesets ir,ads   # 只要伊朗直连 + 广告拦截
```

| id | 名称 | 策略 | 展开成的规则 |
|---|---|---|---|
| `local` | 直连本地 | DIRECT | `GEOIP,LAN`、`GEOIP,private` |
| `cn` | 中国直连 | DIRECT | `GEOSITE,cn`、`GEOIP,CN` |
| `ir` | 伊朗直连 | DIRECT | `GEOSITE,category-ir`、`GEOIP,IR` |
| `cloudflare` | Cloudflare 直连 | DIRECT | `GEOSITE,cloudflare` |
| `apple` | Apple 直连 | DIRECT | `GEOSITE,apple` |
| `microsoft` | 微软直连 | DIRECT | `GEOSITE,microsoft` |
| `steam` | Steam 直连 | DIRECT | `GEOSITE,steam` |
| `onedrive` | OneDrive 直连 | DIRECT | `GEOSITE,onedrive` |
| `ads` | 广告拦截 | REJECT | `GEOSITE,category-ads-all` |

**顺序语义**：clash 是首个命中生效，所以 `rules:` 里 **REJECT 类永远排在 DIRECT 类之前**（否则广告域名会先
命中某条直连规则被放行）；同类之间按**上表顺序**（与勾选先后无关），最后永远是 `MATCH,<漏网之鱼>`。
一个都不选也是合法配置（只剩 `MATCH` 兜底）。

**为什么只用 GEOSITE/GEOIP，不用 `rule-providers`**：`rule-providers` 要 mihomo 在**启动时**去下载规则集，
断网就整份配置起不来，而且没法在本地离线校验。用内置地理数据则是零外部依赖、可 `mihomo -t` 全量验证。
两个实测才知道的坑：

- `mihomo -t` **会**校验 GEOSITE 类别是否存在（报 `list xxx not found in geosite.dat`），所以上面 9 条
  规则集是逐个跑 `mihomo -t` 验过的；
- 但 `mihomo -t` **不校验** GEOIP 国家码（`GEOIP,XX-BOGUS` 也能过），所以国家码只用通用写法。
- 伊朗在 v2fly 与 meta-rules-dat 两份 geosite 里都叫 **`category-ir`**，没有 `ir` 这个类别 —— 这是踩过的坑。

界面里那排复选不是写死在 HTML 里的：列表来自 `GET /api/rulesets`，C++ 侧的 `rule_set_catalogue()`
是唯一真源，避免两边各维护一份。改 `src/emit/rulesets.cpp` 里的目录表即可增删。

## DNS（`dns.nameserver`）与 IPv6

Clash 目标的 `dns.nameserver` 可挑选，命令行 `--dns`，界面在「高级选项」里一排复选：

```powershell
.\build\subconv.exe --list-dns                              # 看全部预设
.\build\subconv.exe -i up.yaml -t clash --dns cloudflare,quad9
.\build\subconv.exe -i up.yaml -t clash --dns 122.112.208.1  # 直接填字面地址
.\build\subconv.exe -i up.yaml -t clash --dns ""             # 不写 nameserver
```

| id | 名称 | 区域 | 地址 |
|---|---|---|---|
| `cloudflare` | Cloudflare | 国外 | 1.1.1.1、1.0.0.1（+IPv6） |
| `google` | Google | 国外 | 8.8.8.8、8.8.4.4（+IPv6） |
| `quad9` | Quad9 | 国外 | 9.9.9.9、149.112.112.112（+IPv6） |
| `adguard` | AdGuard | 国外 | 94.140.14.14、94.140.15.15 |
| `opendns` | OpenDNS | 国外 | 208.67.222.222、208.67.220.220（+IPv6） |
| `alidns` | 阿里 AliDNS | 国内 | 223.5.5.5、223.6.6.6（+IPv6） |
| `dnspod` | 腾讯 DNSPod | 国内 | 119.29.29.29、119.28.28.28（+IPv6） |
| `dns114` | 114 DNS | 国内 | 114.114.114.114、114.114.115.115 |

默认是 `cloudflare,google`（两个国外的）。**默认值是从原来的 `223.5.5.5, 119.29.29.29` 改过来的**：
国内解析器在跨境场景容易被污染/劫持，所以界面上把「国内」几项标成不推荐；在国内用的话自己勾
`alidns` / `dnspod` 就行。

`--dns` 的每一项既可以是预设 id，也可以是**任意字面地址**：IPv4、IPv6、或 `https://<IP>/dns-query`
这样的 DoH。字面量原样透传（去重、保序），所以「华为」这类没内置的解析器直接填地址即可。
顺便说明为什么**没有内置华为**：没查到可信的华为公网解析器地址 —— 华为云文档里公开的
`100.125.1.250` / `100.125.129.250` 是 **VPC 内网**地址，出了华为云不能用；搜索引擎对
「华为 + IP」的查询结果全是华为公司官网，不足以作数。知道确切地址的话填进去就行。

有一个坑要避免：**拼错的预设 id 不会被当成解析器写进配置**（那会让 mihomo 直接拒绝加载），
而是给一条告警并跳过；判断依据是「像不像地址」。另外 DoH 建议写成 `https://<IP>/dns-query`：
用主机名（如 `https://dns.google/dns-query`）需要 mihomo 的 `default-nameserver` 先解析它，
本项目没有生成那一段。

**IPv6** 由 `--ipv6` / `--no-ipv6`（界面上的「IPv6」勾选，默认开）控制，一次改三处：根节点的
`ipv6:`、`dns.ipv6:`、以及**是否带上各 DNS 预设的 IPv6 地址**（关掉时只写 IPv4，免得在没有
IPv6 出口的机器上让 mihomo 去连不存在的 v6 解析器）。目前只有 clash 目标有 DNS / ipv6 段。

## 订阅名称

界面上「订阅名称」放在**高级选项之外**（跟目标、输入同级），填了之后：

- 作为下载文件名，按目标自动补扩展名：`clash` → `.yaml`、`xray`/`singbox` → `.json`、
  `links`/`base64`/`v2rayn` → `.txt`；已经写了扩展名的（如 `我的机场.json`）原样保留，
  `机场 v1.2` 这种单字符后缀不算扩展名，仍会补 `.yaml`。
- 写进配置首行的注释：`# subconv 0.1.0 | target=clash | nodes=19 | name=我的机场`。
- 通过 `/sub` 订阅时，`Content-Disposition` 里会带上它（`filename*=UTF-8''我的机场.yaml`），
  Clash Verge / mihomo Party / Shadowrocket 之类客户端一般就拿它当订阅名。

名字里的 Windows 非法字符（`\ / : * ? " < > |`）与控制字符会被剔掉，路径分隔符也一并去掉，
不会出现「订阅名变成路径穿越」。命令行对应 `--name`；注意 Windows 下 `char** argv` 是
**ACP(GBK)** 编码，所以 `--name` 的值会在解析时先按 ACP 转成 UTF-8 再写进配置（文件路径则
保持 ACP 原样，否则 `fopen` 打不开中文路径）—— 界面走 JSON 本身就是 UTF-8，不受影响。

## Web UI 与 HTTP 接口

```powershell
.\build\subconv.exe serve --open        # 默认 http://127.0.0.1:25500/
```

浏览器界面（单文件、不依赖任何 CDN，已内嵌进二进制）支持：粘贴订阅链接 / 分享链接 / Base64 /
Clash YAML，填**订阅名称**（在高级选项之外），选目标（Clash / Xray / sing-box / v2rayNG 链接 /
v2rayNG 订阅 / v2rayNG 完整），勾选项（emoji、UDP、去重、rules、排序、TFO、IPv6、原版 Clash 语法），
配置代理 / UA / 超时 / 重试，转换后直接复制或下载；界面还会给出可填进客户端的 `/sub` 地址，
并在链接类目标下显示「链接 N 条」。勾上「输出 rules」会展开**分流规则集复选**，「高级选项」里
还有 **DNS 复选 + 自定义地址**（见上两节），这些选择都会记在浏览器里、也会写进生成的 `/sub` 链接。
选「v2rayNG 订阅」时，把生成的那个 `/sub?target=base64&url=…` 地址填进 v2rayNG 的「订阅设置」
就能自动更新。

HTTP 接口是 subconverter 的兼容子集：

| 方法与路径 | 说明 |
|---|---|
| `GET /` | Web UI |
| `GET /api/version` | 版本、已实现目标、libcurl 状态 |
| `GET /api/rulesets` | 分流规则集目录（id / 名称 / 策略 / 说明 + 默认选择） |
| `GET /api/dns` | DNS 预设目录（id / 名称 / 区域 / IPv4 / IPv6 + 默认选择） |
| `POST /api/convert` | JSON 进 JSON 出：`{target, filename, sources[], content, options{}, fetch{}}` |
| `GET /sub?target=&url=` | 返回配置文本；`url` 可用 `\|` 分隔多个，也可重复出现 |
| `GET /clash`、`GET /xray`、`GET /sing-box` | 同上，路径即目标 |

`/sub` 支持的参数：`target`、`url`、`content`、`filename`（订阅名）、`emoji`、`udp`、`tfo`、`sort`、
`dedup`、`rules`、`rulesets`、`dns`、`ipv6`、`clash_legacy`、`proxy`、`ua`、`timeout`、`retries`、
`insecure`、`cache_dir`、`cache_ttl`、`no_cache`。上游带 `subscription-userinfo` 时，
响应头会把它原样透出。

`rulesets` / `dns` 的三种写法要区分清楚（对应 `POST /api/convert` 里 `options.rulesets` /
`options.dns` 的数组或字符串）：

| 写法 | 含义 |
|---|---|
| 不给参数 | 用默认值（`rulesets` → `local,cn`；`dns` → `cloudflare,google`） |
| `rulesets=` / `dns=`（空串），或 `[]` | **一个都不要**：rules 只剩 `MATCH` 兜底；dns 不写 `nameserver` |
| `rulesets=ir,cloudflare` / `dns=quad9,1.2.3.4` | 只填这些（dns 里可混预设 id 与字面地址） |

界面也能用链接预填并自动转换：

```
http://127.0.0.1:25500/?url=<订阅链接>&target=clash&auto=1
```

> 默认只监听 `127.0.0.1`。`--listen 0.0.0.0` 会暴露到局域网，而 `/sub` 允许调用方让服务端
> 去抓取任意 URL（SSRF 面），请只在可信网络里这么用。

## 抓取与容错

- **内容嗅探**：自动区分「分享链接列表 / 整体 Base64 / Clash YAML / JSON 配置 / 网页」。
  机场返回 404 网页或鉴权页时给出明确提示，而不是抛出一堆 YAML 语法错。
- **磁盘缓存**：默认 `%TEMP%\subconv-cache`，同一订阅在 TTL 内不重复请求。
- **离线降级**：网络失败时自动回落到任意龄期的本地缓存，并在告警里说明。
- **`subscription-userinfo`**：解析流量与到期信息并汇总输出到 stderr（M5 会透传成响应头）。

## 内核校验结果

| 目标 | 内核 | 版本 | 结果 |
|---|---|---|---|
| clash | mihomo | v1.19.30 | `test is successful` |
| xray | Xray-core | v26.9.9 | `Configuration OK.` |
| singbox | sing-box | v1.14.0 | exit 0 |

单元测试：**776 项断言全部通过**（含 HTTP 请求映射、转换核心、CA bundle 不变式）。

除内置夹具外，也用真实机场订阅做过端到端验证：一份 19 节点的 Clash YAML 订阅经
`serve` 的 `/sub`、`/xray`、`/sing-box` 三个接口转换后，产物同样通过上述三个内核校验。

## 目录结构

```
订阅转换/
├─ CMakeLists.txt
├─ build.ps1                 构建脚本
├─ include/subconv/          公共头（模型 / 编解码 / YAML / JSON / 主接口）
├─ src/
│  ├─ core/                  数据模型
│  ├─ codec/                 Base64、percent-encoding、URI、字符串工具
│  ├─ parse/                 各协议分享链接 → ProxyNode（含 uri_common 公共参数映射）
│  ├─ emit/                  渲染器：clash / xray / singbox / 分享链接（links、base64）
│  ├─ server/                HTTP 服务 + Web UI（请求映射 / 转换核心 / 传输层三层分离）
│  └─ cli/                   main
├─ data/                     内置规则与模板（M4 起启用）
│  └─ web/index.html         Web UI 源文件；构建时由 CMake 内嵌进二进制
├─ tests/                    单元测试 + fixtures
├─ third_party/nlohmann/     vendor 的 nlohmann/json
└─ tools/                    开发期校验脚本（bin/ 不入库）
```

## 设计要点

- **中间模型统一**：所有协议先解析成 `ProxyNode`，再由各目标渲染器映射，避免字符串拼接。
- **按内核能力裁剪**：Xray 不支持 hysteria2/tuic/ssr/snell，sing-box 不支持 ssr/snell，
  转换时按目标跳过并透出告警，而不是生成加载不了的配置。
- **手写 YAML 输出器**：Clash 配置对 key 顺序与引号敏感，手写才能保证输出确定、可读、可校验。
- **手写 Base64 编解码**：订阅场景里 Base64 变体极多（URL-safe、缺 padding、二次包裹、含空白），
  必须自行掌控容错策略。
- **JSON 走 `ordered_json`**：Xray / sing-box 配置字段顺序稳定，diff 友好。
- **规则集只用内置地理数据**：不依赖远程 `rule-providers`（那会让 mihomo 启动时必须联网下载，
  失败就整份配置起不来），因此产物离线可用、可 `mihomo -t` 全量验证；目录内建在
  `src/emit/rulesets.cpp`，由 `--list-rulesets` 与 `GET /api/rulesets` 共用。
- **控制台输出跟着代码页走**：程序内部字符串一律 UTF-8，但写给人看的提示在 Windows 上会先
  转成控制台代码页（`src/core/console.cpp`）。Windows 控制台默认是 936(GBK)，硬写 UTF-8 中文
  就会显示成「璁㈤槀閾炬帴」；而 PowerShell / cmd 读取子进程输出用的正是控制台代码页，所以
  "让调用方改成 UTF-8" 解决不了。生成的配置载荷则**始终写原始 UTF-8 字节** —— 它要落盘或交给内核。
  终端编码对不上时（例如 PowerShell 7 固定按 UTF-8 解码子进程输出）可用环境变量显式指定：
  `SUBCONV_CONSOLE_ENCODING=utf-8`（或 `936`、`auto`，默认 `auto`）。
- **每个里程碑都用真实内核校验**。

## 已知差异与注意事项

- **Xray v25+ 移除了 `allowInsecure`**：subconv 改用官方迁移路径
  `verifyPeerCertByName`（订阅链接里拿不到证书指纹，无法用 `pinnedPeerCertSha256`）。
- **原版 Clash 语法**（`--clash-legacy`）不支持 vless / hysteria2 / tuic / ssr / snell，
  这些节点会被跳过并写入配置头部注释。
- **HTTPS 抓取报 `error adding trust anchors from file`**：MSYS2 的 `ucrt64` 偶尔会留下
  **0 字节**的 `etc/ssl/certs/ca-bundle.crt`，而 libcurl 的内置默认路径正指向它。subconv 会
  自动退回到 `<MSYS2>/usr/ssl/certs/ca-bundle.crt` 等可用位置，`subconv version` 会打印实际
  生效的路径；也可用环境变量 `CURL_CA_BUNDLE` 指定，或用 `-k` 跳过校验。
- `GEOIP` 规则需要 `geoip.metadb`；mihomo 首次加载会自动下载，离线环境请手动放置。

## 构建环境

| 项 | 版本 |
|---|---|
| 编译器 | MSYS2 UCRT64 `g++` 16.2.0（C++23） |
| 构建 | CMake ≥ 3.20 + Ninja |
| 依赖 | `libcurl`（订阅抓取）、`yaml-cpp`（Clash 订阅解析）、vendor 的 `nlohmann/json` |

> 所有 `.ps1` 脚本以 UTF-8 **带 BOM** 保存 —— Windows PowerShell 5.1 在无 BOM 时会按 ANSI
> 读取，中文注释会导致解析失败。

> 代码里**刻意不用 `std::filesystem`** —— Windows 上它会把窄字符串路径按 UTF-8 转换，
> 而 MinGW 的 `argv` 是 ANSI（中文系统为 GBK），中文路径会抛 `Illegal byte sequence`
> 直接崩溃。改用 CRT 的 `_access` / `_mkdir`（见 `src/core/fsutil.cpp`），与 `std::ofstream`
> 的编码行为保持一致。
