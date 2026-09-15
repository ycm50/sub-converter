# subconv — 用 C++ 写的订阅转换工具

把机场订阅（`ss` / `ssr` / `vmess` / `vless` / `trojan` / `hysteria` / `hysteria2` / `tuic` /
`snell` / `socks5` / `http` 分享链接、Clash YAML）转换成各客户端可直接加载的配置，并提供兼容
[subconverter](https://github.com/tindy2013/subconverter) 的 HTTP `/sub` 接口与内置 Web UI。

- **三种用法**：命令行、HTTP 接口（`/sub`、`/clash`、`/xray`、`/sing-box`）、浏览器界面（`serve`）。
- **六个输出目标**：`clash`(mihomo) / `xray` / `singbox` / `links` / `base64` / `v2rayn`。
- **不依赖远程模板**：分流规则只用内核自带的 GEOSITE/GEOIP，DNS 预设内建，产物离线可用、
  可被真实内核 `-t` / `check` 全量校验；Web UI 已内嵌进二进制，拷到哪都能跑。
- **按目标自动裁剪**：Xray 不支持 hysteria2/tuic/ssr/snell，sing-box 不支持 ssr/snell，
  遇到不支持的协议会跳过并给出告警，而不是产出加载不了的配置。

## 目录

- [特性](#特性)
- [当前进度](#当前进度)
- [输入与输出](#输入与输出)
- [协议 × 目标 支持矩阵](#协议--目标-支持矩阵)
  - [传输层（network）× 目标](#传输层network--目标)
  - [证书校验：为什么同一份订阅在 Clash 里能用、在 Xray 里全是 -1](#证书校验为什么同一份订阅在-clash-里能用在-xray-里全是--1)
  - [VLESS Encryption：为什么节点连不上却没有任何报错](#vless-encryption为什么节点连不上却没有任何报错)
- [快速开始](#快速开始)
- [支持的平台](#支持的平台)
- [从源码构建](#从源码构建)
- [命令行参考](#命令行参考)
- [客户端导入](#客户端导入)
- [分享链接输出（v2rayNG）](#分享链接输出v2rayng)
- [分流规则集（rules，仅 Clash 目标）](#分流规则集rules仅-clash-目标)
- [DNS（dns.nameserver）与 IPv6](#dnsdnsnameserver与-ipv6)
- [订阅名称](#订阅名称)
- [Web UI 与 HTTP 接口](#web-ui-与-http-接口)
- [抓取与容错](#抓取与容错)
- [故障排查（FAQ）](#故障排查faq)
- [内核校验](#内核校验)
- [开发与测试](#开发与测试)
- [CI 与发布（GitHub Actions）](#ci-与发布github-actions)
- [目录结构](#目录结构)
- [设计要点](#设计要点)
- [已知差异与注意事项](#已知差异与注意事项)
- [构建环境](#构建环境)
- [许可证](#许可证)

## 特性

- **协议覆盖全**：11 种分享链接形态 + 上游 Clash YAML（含 JSON 版），含 vmess 的 ws/grpc/h2/http、
  vless 的 reality、**vless 的 xhttp（含 `download-settings` 上下行分流）**、**vless 的
  `encryption`（VLESS Encryption / XTLS Vision Seed）**、ss 的 obfs / v2ray-plugin、
  hysteria2 的 obfs 等传输细节。
- **手写输出器**：YAML / JSON / Base64 / URI 全部自己渲染 —— 订阅场景里 Base64 变体极多，
  而 Clash 配置对 key 顺序与引号敏感，只有自己掌控才能保证输出确定、可读、可校验。
- **两端对齐分享链接**：`links` / `base64` / `v2rayn` 的形态是逐字段核对 v2rayNG(`com.v2ray.ang.fmt.*Fmt`)
  与 v2rayN(`ServiceLib/Handler/Fmt/*`) 源码得出的，并用往返测试（生成 → 自己解析回来 → 逐字段比对）保证。
- **真实内核校验**：每个目标都用 mihomo / Xray / sing-box 的真实二进制校验产物。
- **Windows 编码认真处理**：内部字符串一律 UTF-8，控制台输出按代码页转换，中文订阅名与中文路径都可用。
- **一份源码、九个平台**：Linux（x86_64 / aarch64 / armv7l / i686 / riscv64）、
  Windows（x86_64 / aarch64）、Termux（aarch64 / arm），CI 一次全编出来。
  平台相关代码只集中在几处 `#ifdef _WIN32`，Windows 用 `.\build.ps1`、其它平台用 `./build.sh`。

## 当前进度

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 工具链 + CMake 骨架 | ✅ |
| M1 | `ss`/`socks5`/`http` 解析 + Clash YAML 输出 | ✅ |
| M2 | 全协议解析 + **Xray** / **sing-box** 目标 | ✅ |
| M3 | libcurl 订阅抓取（代理 / 重定向 / 缓存 / 内容嗅探） | ✅ |
| M4 | 规则模板引擎 + 分组生成 + 过滤 / 重命名 / emoji | 🚧 分组与**可挑选的分流规则集**已内建（见「[分流规则集](#分流规则集rules仅-clash-目标)」）；模板外置 `data/`、节点过滤 / 重命名待做 |
| M5 | HTTP `/sub` 服务 + **Web UI** | ✅ 见「[Web UI 与 HTTP 接口](#web-ui-与-http-接口)」 |
| M6 | golden file 测试 + **多平台发布** + 文档 | ✅ Release 一次挂 9 个平台（Linux 5 架构 / Windows 2 架构 / Termux 2 架构），见「[支持的平台](#支持的平台)」；golden file 测试仍待做 |

> **当前状态**：M0–M3、M5 已完成，M4 进行中。
> Clash 目标已生成 3 个 emoji 分组（🚀 节点选择 / ♻️ 自动选择 / 🐟 漏网之鱼，`--no-emoji` 可关闭）
> 与可挑选的 rules（默认 5 条：`GEOIP,LAN` / `GEOIP,private` / `GEOSITE,cn` / `GEOIP,CN` / `MATCH`，
> 用 `--rulesets` 或 Web UI 的复选增减）；
> 另有可挑选的 `dns.nameserver`（默认 `cloudflare,google`，**不再是原来的 223.5.5.5**）与
> `ipv6`（默认 **开**），见「[DNS 与 IPv6](#dnsdnsnameserver与-ipv6)」；还支持设置订阅名称，
> 见「[订阅名称](#订阅名称)」。
> Xray / sing-box 目标只含入站出站结构，不含分组、规则与 DNS。另有 `links` / `base64` 两个目标，
> 导出 v2rayNG / v2rayN / Shadowrocket 通用的分享链接（详见「[分享链接输出](#分享链接输出-v2rayng)」）。
> 所有目标的产物都已用真实内核校验通过。`data/` 目前只有 `web/`（Web UI 源文件）与说明文档；
> 分流规则集与 DNS 预设刻意留在代码里（`src/emit/rulesets.cpp`、`src/emit/dns.cpp`），原因见 `data/README.md`。
>
> **2026-09 补充**：支持机场新主流的 `network: xhttp` 传输（含 `download-settings` 上下行分流），
> 并针对「Xray 25+ 移除 `allowInsecure` 导致客户端全部 -1」加了 `--probe-cert` 证书指纹探测，
> 见「[传输层 × 目标](#传输层network--目标)」与「[证书校验](#证书校验为什么同一份订阅在-clash-里能用在-xray-里全是--1)」。
>
> **2026-09 补充 2**：修掉两个输入侧的坑 —— Web UI 输入框曾经把每行 `trim` 后拼回去，粘贴的
> Clash YAML 会因丢缩进报 `end of map not found`；面板 `?app=clash` 返回的 **JSON 版 Clash 配置**
> 曾被当成「不支持的 JSON 配置」拒掉，现在按 Clash YAML 解析。
>
> **2026-09 补充 3**：支持 Xray 的 **VLESS Encryption**（`encryption=mlkem768x25519plus.…`）。
> 之前这个字段在解析时会被丢弃，产物里节点不缺不加、客户端却全部超时（服务端解不开 VLESS 头部
> 就既不回包也不关连接）。现在链接 / Clash YAML 两种输入都解析它，clash / xray / links / v2rayn
> 四个目标都原样透传并计入去重指纹，sing-box 目标明确跳过并告警 —— 见
> 「[VLESS Encryption](#vless-encryption为什么节点连不上却没有任何报错)」。

**目标优先级**：Clash(mihomo) → Xray → sing-box。

## 输入与输出

### 支持的输入形态

`-i` / `--input` 可重复，多个输入会合并；`/sub?url=` 支持重复出现或用 `|` 分隔（`url` / `urls` / `s` 三个参数名等价）。

| 形态 | 说明 |
|---|---|
| 分享链接列表 | 每行一条，`ss://` `ssr://` `vmess://` `vless://` `trojan://` `hysteria://` `hysteria2://`（含 `hy2://`）`tuic://` `snell://` `socks5://`（含 `socks://`）`http://` `https://`，允许空行与注释行 |
| 整体 Base64 | 上面列表的 Base64（标准或 URL-safe、缺 padding、含空白都能解；**最多自动解两层包裹**，部分机场会二次编码；自动跳过 UTF-8 BOM） |
| Clash YAML | 上游 Clash / mihomo 配置，取其 `proxies:` 列表（需要 yaml-cpp，见「[从源码构建](#从源码构建)」）；含 `network: xhttp` + `xhttp-opts`（含 `download-settings`）的节点会完整还原。**JSON 版的 Clash 配置也走这条路**：JSON 是 YAML 的子集，mihomo 直接吃，很多面板的 `?app=clash` 返回的就是 JSON（`content-type: application/json`） |
| JSON 配置 | sing-box / Xray / v2ray 的 JSON 配置 ❌ 暂不支持：识别出来会**明确报错**「这是 JSON 配置而不是订阅」，而不是抛一堆语法错（Clash 方言的 JSON 见上一行） |
| 网页 | ❌ 识别为 HTML 时给出「机场错误页 / 需要鉴权 / 链接失效」提示 |

`https://user:pass@host:port` 形式的链接会解析成**带 TLS 的 http 代理**；由于分享链接无法携带证书信息，
这类节点按社区惯例默认跳过证书校验（`tls.insecure = true`）。

### 输出目标

| 目标 | 产物 | 默认扩展名 | 典型去处 |
|---|---|---|---|
| `clash` | mihomo 完整配置（`proxies` + 分组 + `rules` + `dns` + `ipv6`） | `.yaml` | mihomo、Clash Verge、mihomo Party、ClashX Meta |
| `xray` | Xray-core 完整配置（inbounds + outbounds + routing + balancer） | `.json` | Xray-core、v2rayN(Xray 内核) |
| `singbox` | sing-box 完整配置（inbounds + selector/urltest + route） | `.json` | sing-box 官方客户端 |
| `links` | 每行一条分享链接 | `.txt` | v2rayNG「从剪贴板导入」、Shadowrocket |
| `base64` | 上面列表再整体 Base64 | `.txt` | 任意客户端的订阅 URL 内容 |
| `v2rayn` | 每行一条 `v2rayn://<协议>/<base64url(JSON)>` | `.txt` | v2rayN / v2rayNG —— **唯一能承载 http(s) 代理的形态** |

`--list-targets` 会打印已实现与规划中的目标（规划中：`surge`、`loon`、`quanx`、`surfboard`、`stash`、`mixed`）。

## 协议 × 目标 支持矩阵

各内核能力边界不同，subconv 会**按目标自动裁剪并给出告警**，而不是产出无法加载的配置。

| 协议 | clash (mihomo) | xray | sing-box | v2rayNG |
|---|---|---|---|---|
| shadowsocks | ✅ 含 obfs / v2ray-plugin | ⚠️ 仅 AEAD / 2022 系列，旧加密自动跳过 | ✅ 含插件 | ✅ 插件以 `plugin=` 回写 |
| shadowsocksr | ✅ | ❌ 不支持 | ❌ 不支持 | ❌ 不支持 |
| vmess | ✅ ws/grpc/h2/http | ✅ | ✅ | ✅ |
| vless + reality | ✅ | ✅ | ✅ | ✅ |
| vless + encryption | ✅ 实测 | ✅ 实测 | ❌ 内核无此字段，跳过并告警 | ✅ 实测 |
| trojan | ✅ | ✅ | ✅ | ✅ |
| hysteria v1 | ✅ | ❌ 不支持 | ✅ | ❌ 只支持 hysteria2 |
| hysteria2 | ✅ | ❌ 不支持 | ✅ | ✅ |
| tuic | ✅ | ❌ 不支持 | ✅ | ❌ 未启用 |
| snell | ✅ | ❌ 不支持 | ❌ 不支持 | ❌ 不支持 |
| socks5 / http | ✅ | ✅ | ✅ | ✅ socks5 / ⚠️ http 需用 `-t v2rayn` |
| wireguard | ⏳ 未实现 | ⏳ | ⏳ | ⏳ |

> 「v2rayNG」这一列指 `links` / `base64` 目标产出的分享链接能否被 v2rayNG 导入；
> v2rayNG 是 Xray/v2fly 客户端，**不支持 ssr、snell、hysteria v1、tuic**，也不认识 `http://` 分享链接。

### 传输层（`network`）× 目标

协议之外，传输层同样会按目标裁剪 —— 2025 年底起机场普遍改用 **xhttp**（Xray 旧名 `splithttp`，
`network: xhttp` + `xhttp-opts`），三个内核对它的支持差别最大：

| 传输层 | clash (mihomo) | xray | sing-box | v2rayNG |
|---|---|---|---|---|
| tcp / raw | ✅ | ✅ | ✅ | ✅ |
| ws | ✅ | ✅ | ✅ | ✅ |
| grpc | ✅ | ✅ | ✅ | ✅ |
| h2 / http | ✅ | ✅ | ✅ | ✅ |
| **xhttp / splithttp** | ⚠️ **仅 vless 出站**（vmess/trojan 跳过） | ✅ vmess / vless / trojan | ❌ 没有该传输，节点全部跳过 | ✅（`type=xhttp`） |

- **mihomo**：`transport/xhttp` 只被 `adapter/outbound/vless.go` 引用，vmess / trojan 的
  `StreamConnContext` 里没有这个 case，所以非 vless 的 xhttp 节点会被跳过并告警。
- **sing-box**：官方 V2Ray 传输是枚举死的（`option/v2ray_transport.go` 的
  `enum:"http,ws,quic,grpc,httpupgrade"`），`sing-box check` 会直接报
  `unknown transport type: xhttp`。subconv 因此**跳过**这些节点，而不是把它们降级成 tcp
  （那样能过 check，但连不上）。
- **`download-settings`**（下载走另一台机器）在 clash / xray 目标里都按各自 schema 还原：
  mihomo 是 `xhttp-opts.download-settings`（含 `server`/`port`/`servername`/`tls`），
  Xray 是 `xhttpSettings.downloadSettings`（一份完整的 StreamConfig：`address`/`port`/
  `network`/`security`/`tlsSettings`/`xhttpSettings`）。未显式写出的字段保持"沿用主节点"
  的语义（用 `optional` 表达），不会被填成默认值。
- **分享链接**：`type=xhttp` + `host`/`path`/`mode` + `extra=<JSON>`，字段名与
  v2rayN `BaseFmt.ToUriQuery` / v2rayNG `FmtBase.emitTransportQuery` 一致；`extra` 里的
  高级参数（`xmux`、`xPaddingBytes` 等）在 `links` / `base64` / `v2rayn` 与 **xray** 目标里
  **原样透传**（Xray 侧 `extra` 会整体覆盖离散字段，这是内核行为），mihomo 没有 `extra`
  概念，遇到时会额外告警说明只保留了 `download-settings`。

### 证书校验：为什么同一份订阅在 Clash 里能用、在 Xray 里全是 `-1`

机场常见的写法是 `skip-cert-verify: true` + 一个**与自己域名无关的 SNI**。实测一元机场
（`sub1.smallstrawberry.com`，2026-09 的订阅）：

```
openssl s_client -connect hk1.<...>.the-best-airport.com:443 -servername update.microsoft.com
  Peer certificate: CN=new.download.the-best-airport.com      ← 证书名与 SNI 对不上
  Verification: OK                                            ← 链本身是可信的
```

各内核对「跳过证书校验」的支持并不一样，这就是差异的根源：

| 目标 | `skip-cert-verify` 的表达 | 结果 |
|---|---|---|
| clash (mihomo) | `skip-cert-verify: true`（mihomo 仍支持） | ✅ 实测 14/18 个活节点拿到真实延迟（1.3–3.6 s） |
| xray | ❌ 没有等价开关 | ⚠️ 见下 |
| sing-box | `tls.insecure` | ✅（但这份订阅是 xhttp，节点全部被跳过，见上） |
| v2rayN / v2rayNG（Xray 内核） | 链接里的 `pcs=` 指纹 | ⚠️ 见下 |

Xray 25 起**彻底移除**了 `allowInsecure`，加载配置时会直接报：

```
The feature "allowInsecure" has been removed and migrated to
"pinnedPeerCertSha256"(pcs) and "verifyPeerCertByName"(vcn).
```

而两条替代路径的语义并不相同（`transport/internet/tls/config.go` 的
`RandCarrier.verifyPeerCert`）：

- `pinnedPeerCertSha256`：命中**叶子证书**就 `return nil`，不校链、不校名 —— 等价于
  `skip-cert-verify`；
- `verifyPeerCertByName`：仍然要求「证书链可信 **且** 名字匹配」，上面那种证书与 SNI
  对不上的节点**必然失败**：
  `transport/internet/tls: peer cert is invalid (against root CAs and verifyPeerCertByName)`。
  客户端里的表现就是**所有节点延迟 `-1`** —— 网上那些「转出来全是 -1」的订阅多半栽在这里。

所以 subconv 提供了 `--probe-cert`（Web UI 勾「探测证书指纹」，HTTP `probe_cert=1`）：
转换前主动连一次每个「要求跳过校验」的节点（含 xhttp 的下载侧），把叶子证书的 SHA256
写成 Xray 的 `pinnedPeerCertSha256` / 分享链接的 `pcs=` / v2rayN 的 `CertSha`。
没探测到指纹的节点会**告警**并退回 `verifyPeerCertByName`，而不是假装没事。

```powershell
.\build\subconv.exe -i sub.yaml -t xray  -o out.json --probe-cert   # Xray 内核可用
.\build\subconv.exe -i sub.yaml -t links -o nodes.txt --probe-cert  # v2rayN / v2rayNG 可用
```

> 实测（同一台机器、同一份订阅）：`xray run -test` 通过后，配置里的 hk1 节点连续 3 次
> `curl --socks5-hostname` 请求 `http://www.gstatic.com/generate_204` 全部返回
> `HTTP/1.1 204 No Content`；不加 `--probe-cert` 时同一节点报
> `peer cert is invalid (against root CAs and verifyPeerCertByName)`，一个都不通。

### VLESS Encryption：为什么节点连不上却没有任何报错

2026 年起 Xray 给 VLESS 加了一层**协议内加密**（官方叫「VLESS 加密」，也叫 XTLS Vision Seed：
用 ML-KEM-768 + X25519 混合协商后量子安全的会话密钥），节点参数长这样：

```
mlkem768x25519plus.native.0rtt.100-111-1111.75-0-111.50-0-3333.<客户端认证公钥>
```

块之间用 `.` 分隔，含义固定：

| 块 | 取值 | 说明 |
|---|---|---|
| 1 | `mlkem768x25519plus` | 握手算法，目前只有这一个 |
| 2 | `native` / `xorpub` / `random` | 客户端公钥的伪装形态 |
| 3 | `0rtt` / `1rtt` | 往返次数 |
| 4… | `prob-min-max`（可多段） | padding / delay 策略，第一段必须是 `100` 且 `min > 0` |
| 末段 | 客户端认证公钥 | `xray mlkem768` 生成的 Client 部分 |

中间块省略时内核用默认值补齐（padding 缺省为 `100-111-1111.75-0-111.50-0-3333`）。
`encryption` **不能为空**——要关闭必须显式写 `none`。subconv 只对结构明显不合法的值
（块数不足、未知算法/形态、概率越界、首段 padding 非 `100` 或 `min = 0`、缺少公钥段……）
记一条校验告警，值本身**原样透传，绝不改写或丢弃**（`src/core/vless_encryption.cpp`）。

**这个字段只有 Xray / mihomo 认识**，各目标的落地情况：

| 目标 | 支持 | 落点 |
|---|---|---|
| clash (mihomo) | ✅ 实测 v1.19.31 | 出站 `encryption:` |
| xray | ✅ 实测 26.3.27 | `outbounds[].settings.vlessSettings.users[].encryption`（未设置时显式写 `"none"`） |
| 分享链接 / v2rayNG | ✅ | 链接 query 的 `encryption=`；`v2rayn` 目标写 `ProtoExtraObj.VlessEncryption` |
| sing-box | ❌ | 1.14 的 VLESS 出站没有该字段（内核二进制里搜不到 `mlkem768x25519plus`），节点被跳过并告警 |

**踩坑点在于：丢了 `encryption` 不报错，而是「静默黑洞」。** 服务端拿不到它就解不开 VLESS 头部，
于是**既不回包也不关连接**，客户端只能干等到 dial timeout —— 表现是整份订阅全部超时 / `-1`，
日志里一句有用的错误都没有。最典型的场景是「同一份节点直连 mihomo 能用，过一层订阅转换就全超时」：
中间那层把没见过的 query 参数丢掉了。所以 subconv 把它当成一等字段：解析
（链接的 `encryption=`、Clash YAML 的 `encryption:`）、逐目标落地、并计入去重指纹
（同一服务器、不同认证公钥的节点不会被合并）。

它和 `flow: xtls-rprx-vision` 还有一层约束：**XTLS Vision 只允许出现在「tcp + TLS / REALITY」
或「已启用 VLESS Encryption」两种情况**。所以 `type=ws` + `flow=xtls-rprx-vision` +
`encryption=…` 是**合法**组合；而一旦 `encryption` 在中间层丢失，mihomo 会直接拒绝该节点：

```
vision: not a valid supported TLS connection
```

subconv 对这种「带 vision、但既不是 tcp+TLS 也没开 encryption」的节点也会告警 —— 它正是识别
「中间层丢字段」最直接的信号。

## 快速开始

Windows（PowerShell）：

```powershell
# 1. 构建（Release + 单元测试）
.\build.ps1 -Test

# 2. 生成测试夹具（可选，覆盖 19 节点 / 11 种协议 + xhttp 传输）
.\tests\fixtures\generate.ps1

# 3. 转换：本地文件 / URL / Clash YAML 都可以作为输入
.\build\subconv.exe -i tests\fixtures\all_protocols_b64.txt -t clash   -o out.yaml -v
.\build\subconv.exe -i https://example.com/sub -t clash -o out.yaml --proxy socks5://127.0.0.1:10808
.\build\subconv.exe -i upstream-clash-config.yaml -t xray -o out.json

# 3.1 订阅里的节点要求跳过证书校验（机场 xhttp 订阅基本都这样）时，转 xray/links 要加这个，
#     否则 Xray 内核拿不到指纹会全部握手失败（客户端里所有节点 -1）
.\build\subconv.exe -i https://example.com/sub -t xray -o out.json --probe-cert

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

Linux / Termux 只需要把构建那一步换成 `./build.sh -t`（其余的命令把 `.\build\subconv.exe`
换成 `./build/subconv`、路径分隔符换成 `/` 即可）：

```bash
./build.sh --test                                       # 等价于 .\build.ps1 -Test
./build/subconv -i https://example.com/sub -t clash -o out.yaml
./build/subconv serve --open                            # Web UI 就在浏览器里打开
```

不知道有没有预编译包可以直接下载？见「[支持的平台](#支持的平台)」。构建细节见「[从源码构建](#从源码构建)」。

生成的 Clash 配置长这样（节选，`--name 示例机场`）：

```yaml
# subconv 0.1.0 | target=clash | nodes=18 | name=示例机场
# 由 subconv 自动生成；分组与规则集内建于程序（--list-rulesets / --list-dns）
mixed-port: 7890
allow-lan: false
mode: rule
log-level: info
ipv6: true
external-controller: 127.0.0.1:9090
dns:
  enable: true
  ipv6: true
  enhanced-mode: redir-host
  nameserver:
    - 1.1.1.1
    - 1.0.0.1
    - 2606:4700:4700::1111
    - 2606:4700:4700::1001
    # …（google 的四个地址）
proxies:
  - name: SS-Node
    type: ss
    server: ss.example.com
    port: 8443
    cipher: aes-256-gcm
    password: sspass
    udp: true
  # …
proxy-groups:
  - name: 🚀 节点选择
    type: select
    proxies:
      # 全部节点 …
      - DIRECT
  - name: ♻️ 自动选择
    type: url-test
    url: http://www.gstatic.com/generate_204
    interval: 300
    proxies:
      # 全部节点 …
  - name: 🐟 漏网之鱼
    type: select
    proxies:
      - 🚀 节点选择
      - ♻️ 自动选择
      - DIRECT
rules:
  - GEOIP,LAN,DIRECT,no-resolve
  - GEOIP,private,DIRECT,no-resolve
  - GEOSITE,cn,DIRECT
  - GEOIP,CN,DIRECT
  - MATCH,🐟 漏网之鱼
```

## 支持的平台

同一份源码，CI 会在发版时把下面这些平台的包都编出来，一起挂在 Release 里：

| 平台 | 产物名（`<tag>` 形如 `v1.2`） | 怎么编的 | 目标机需要什么 |
|---|---|---|---|
| Linux x86_64 | `subconv-<tag>-linux-x86_64.tar.gz` | Ubuntu 22.04 + GCC 12，原生 | glibc ≥ 2.35，`libcurl4` `libssl3` |
| Linux aarch64 | `subconv-<tag>-linux-aarch64.tar.gz` | Ubuntu 22.04 arm64，原生 | 同上 |
| Linux armv7l / i686 | `subconv-<tag>-linux-<arch>.tar.gz` | Debian 12 容器 + QEMU 模拟（GCC 12） | glibc ≥ 2.36，`libcurl4` `libssl3` |
| Linux riscv64 | `subconv-<tag>-linux-riscv64.tar.gz` | Ubuntu 24.04 容器 + QEMU 模拟（GCC 13） | glibc ≥ 2.39，`libcurl4t64` `libssl3t64` |
| Windows x86_64 | `subconv-<tag>-windows-x86_64.zip` | **Linux 上交叉编译**（llvm-mingw / UCRT，`x86_64-w64-mingw32`） | 什么都不用装（单个静态 exe） |
| Windows aarch64 | `subconv-<tag>-windows-aarch64.zip` | 同上（`aarch64-w64-mingw32`） | 同上 |
| Termux aarch64 / arm | `subconv-<tag>-termux-<arch>.tar.gz` | 官方 `termux/termux-docker` 工具链（bionic + libc++） | `pkg install libc++ libcurl openssl` |

> 已按需舍去 `linux-ppc64le`、`linux-s390x`、`windows-i686`、`termux-i686`、`termux-x86_64`。
> 连带的影响：**没有大端平台了**，字节序相关的回归不再有 CI 覆盖（代码里只有显式 `ntohs`
> 和按字节移位，理论上无碍，但已无实测）。
>
> QEMU 那两条线的基础镜像不一样，是因为 `debian:bookworm` 的官方镜像没有 riscv64
> （只有 386/amd64/armv7/arm64/ppc64le），riscv64 只能用 `ubuntu:24.04`，所以 glibc 基线也不同。

每个包里都有 `README.md` 和 `HOW-TO-RUN.txt`（写明解包后怎么跑、需要哪些运行库）。

**为什么 Linux 包不依赖目标机上的 `libyaml-cpp`**：Ubuntu 22.04 打的包是 0.7、24.04 是 0.8，
动态链接出去必然有一头起不来（`libyaml-cpp.so.0.7: cannot open shared object file`）。
`--vendor-yaml`（CI 里默认打开）会把 yaml-cpp 从源码**静态**编进二进制，于是只剩 `libcurl` / `libssl`
两个几乎所有发行版都自带、一条命令就能装上的运行库。

macOS 不在支持范围内：CI 不编、不发包，代码里也没有 `__APPLE__` 分支了。

> `--vendor-yaml` 在 **CMake 4** 上需要一点配合：yaml-cpp 0.8.0 自己声明的是
> `cmake_minimum_required(VERSION 3.4)`，而 CMake 4.0 起不再兼容 < 3.5 的工程，会直接报
> 「Compatibility with CMake < 3.5 has been removed」。CMakeLists 里因此设了官方给的过渡开关
> `CMAKE_POLICY_VERSION_MINIMUM=3.5`（CMake 3.31+ 才认识，更老的版本只是忽略它）。
> MSYS2 和 Termux 的工具链自带的 cmake 都是 4.x，本地用 CMake 4 交叉编译 Windows 时同理，
> 不设就是配置阶段直接失败。

## 从源码构建

### 前置依赖

硬依赖只有 **CMake ≥ 3.20** 和 **Ninja**，外加一个支持 `std::expected` 的 C++23 编译器
（**GCC ≥ 12 / Clang ≥ 16 / MSVC ≥ 19.33** —— 注意 Ubuntu 22.04 自带的 g++ 是 11，太老，要显式装 12+）。
libcurl / OpenSSL / yaml-cpp 三个都是**可选**依赖，装不全也能编过，只是对应功能停用。

```bash
# Debian / Ubuntu
sudo apt install build-essential g++-12 cmake ninja-build \
                 libcurl4-openssl-dev libssl-dev libyaml-cpp-dev

# Fedora / RHEL
sudo dnf install gcc-c++ cmake ninja-build libcurl-devel openssl-devel yaml-cpp-devel

# Arch
sudo pacman -S base-devel cmake ninja curl openssl yaml-cpp

# Termux（手机上的 Linux）
pkg install clang cmake ninja libcurl openssl yaml-cpp
```

Windows 用 **Linux 上的 llvm-mingw 交叉编译**（CI 走的就是这条路，不再依赖 MSYS2）：

```bash
# 在 Linux / WSL 里执行（仓库放在哪都行）
tools/setup-llvm-mingw.sh                     # 下载 llvm-mingw（UCRT）到 .cache/llvm-mingw
tools/build-windows-deps.sh x86_64-w64-mingw32 \
    .cache/llvm-mingw .cache/win-deps/x86_64  # 交叉编静态 OpenSSL + libcurl（约 5 分钟）

export PATH="$PWD/.cache/llvm-mingw/bin:$PATH"
cmake -S . -B build-win-x86_64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=tools/mingw-w64-toolchain.cmake \
  -DSUBCONV_MINGW_TRIPLE=x86_64-w64-mingw32 \
  -DSUBCONV_WINDOWS_SYSROOT="$PWD/.cache/win-deps/x86_64" \
  -DSUBCONV_VENDOR_YAMLCPP=ON
cmake --build build-win-x86_64
```

> 换成 `aarch64-w64-mingw32` + `.cache/win-deps/aarch64` 就是 arm64 Windows 的产物。
> CI 里**不运行** Windows 产物（不装 Wine），两个架构都只保证「编得出来 + 链接通过 + 架构断言正确」；
> 要真跑，把 exe 拿到 Windows 机器上执行 `subconv.exe version` / `subconv_tests.exe` 即可。
> 国内网络慢的话套个 GitHub 镜像：`SUBCONV_GH_MIRROR=https://gh-proxy.com/` ——
> 两个脚本和 CMake 都认这个变量（CMake 那边用它下载内置的 yaml-cpp，见
> `-DSUBCONV_VENDOR_YAMLCPP=ON`），不设就是直连 GitHub。
>
> 32 位（`i686-w64-mingw32`）也编得出来，但**已不在 CI 里**。

> MSYS2 原生构建（`build.ps1`，`pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,curl,openssl,yaml-cpp}`）
> 仍然可用，但 CI 已经不走它了：那种环境下 `MINGW_PREFIX`、pkg-config 和原生 CMake 之间的
> 路径形式太容易互相打架（下面「[CI 与发布](#ci-与发布github-actions)」里记了踩过的坑）。

### 构建

```bash
# Linux / Termux
./build.sh                      # Release 构建
./build.sh --test               # 构建并跑单元测试
./build.sh --clean --test       # 先清空 build 目录
./build.sh --config Debug       # 指定构建类型（默认 Release）
./build.sh --vendor-yaml        # 把 yaml-cpp 源码静态编进去（发行版只有 0.7、或没这个包时用）
./build.sh --static-runtime     # 静态链接 libstdc++ / libgcc
./build.sh --version v1.0       # 覆盖二进制里显示的版本号
./build.sh --prefix /usr/local  # 依赖装在非默认前缀时指路（可写多个，用 : 分隔）
```

```powershell
# Windows
.\build.ps1                 # Release 构建
.\build.ps1 -Test           # 构建并运行单元测试
.\build.ps1 -Clean          # 先清空 build 目录
.\build.ps1 -Config Debug   # 指定构建类型（默认 Release）
.\build.ps1 -BuildDir b2    # 指定构建目录（默认 build）
```

> 如果 `./build.sh` 报 `Permission denied`（Windows 上检出经常丢掉可执行位），
> 直接 `bash build.sh …` 或 `chmod +x build.sh` 即可。

`build.ps1`（MSYS2 原生构建，仍可用）会把 `A:\msys64\ucrt64\bin` 加进 `PATH` 并把它作为
`CMAKE_PREFIX_PATH`（可用环境变量 `MSYSTEM_PREFIX` 覆盖）。`build.sh` 同样会自动指路：Termux 认
`$PREFIX`，也可以直接 `--prefix` 指定。CMakeLists 里还会探测 `MSYS2_PREFIX` / `MSYSTEM_PREFIX` /
`MINGW_PREFIX`，并优先**从编译器路径反推** MSYS2 前缀 —— 但 Windows 交叉编译用不到这些：
`tools/mingw-w64-toolchain.cmake` 直接拿 `SUBCONV_WINDOWS_SYSROOT` 当查找根，不碰 MSYS2 那套挂载点。

> 交叉编译时 CMake **只能**在 `SUBCONV_WINDOWS_SYSROOT` 里找库和头文件
> （`CMAKE_FIND_ROOT_PATH_MODE_LIBRARY/INCLUDE/PACKAGE = ONLY`），免得误抓宿主机 Linux 的
> libcurl / OpenSSL。libcurl / libssl 是静态链的，所以缺的那几个 Windows 系统库
> （`ws2_32` / `gdi32` / `crypt32` / `iphlpapi` / `bcrypt`）挂在工具链文件的
> `CMAKE_CXX_STANDARD_LIBRARIES` 末尾 —— 静态库的 `-l` 顺序不能乱。这里有个 CMake 的坑：
> 只能写成 **cache** 变量（`Platform/Windows-GNU.cmake` 会在工具链文件之后把
> `CMAKE_*_STANDARD_LIBRARIES_INIT` 覆盖成它自己那份 mingw 默认库，而 `CMakeCXXInformation.cmake`
> 是「cache 里没有才拿 `_INIT` 填」），普通变量会被丢掉。另外工具链里设了
> `CURL_USE_STATIC_LIBS=ON`，`FindCURL` 才会挂上 `CURL_STATICLIB` 宏，不然 curl 的头文件
> 在 Windows 上把 `curl_easy_*` 声明成 `__declspec(dllimport)`，静态链 libcurl 会直接报
> 「is available in libcurl.a but cannot be used because it is not an import library」。

也可以直接手工构建（各平台通用）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### CMake 选项

| 选项 | 默认 | 作用 |
|---|---|---|
| `SUBCONV_BUILD_TESTS` | ON | 构建 `subconv_tests` |
| `SUBCONV_USE_CURL` | ON | 启用 libcurl 订阅抓取 |
| `SUBCONV_USE_OPENSSL` | ON | 启用 `--probe-cert` 证书指纹探测 |
| `SUBCONV_USE_YAML` | ON | 用 yaml-cpp 解析 Clash 订阅 |
| `SUBCONV_VENDOR_YAMLCPP` | OFF | 从源码**静态**编译 yaml-cpp（发行版只有 0.7 包、或干脆没这个包时用；Linux 打包和 Windows 交叉编译都开着，需要能访问 GitHub，可以用 `SUBCONV_GH_MIRROR` 走镜像） |
| `SUBCONV_GH_MIRROR` | 空（取同名环境变量） | GitHub 镜像前缀，例如 `https://gh-proxy.com/`，只影响内置 yaml-cpp 的下载地址 |
| `SUBCONV_STATIC_RUNTIME` | OFF | 静态链接 libstdc++ / libgcc，少一个运行库依赖（配合 `SUBCONV_VENDOR_YAMLCPP` 用最安全，能避免出现两份 libstdc++） |
| `SUBCONV_VERSION` | `0.1.0`（`project()` 里那个） | 对外显示的版本号；CI 用发布 tag 覆盖它，例如 `-DSUBCONV_VERSION:STRING=1.0`（**要写 `:STRING=`**，否则 CMake 会把 `1.0` 当数字规范成 `1`） |

C++ 标准不是直接写 `CMAKE_CXX_STANDARD 23` 了事：`std::expected` 需要 C++23，但 GCC 11/12 只认
`-std=c++2b` 这个名字，而不同版本的 CMake 对 `23` 的映射又不一样。CMakeLists 会现场探测
`-std=c++23` / `-std=c++2b`（MSVC 是 `/std:c++latest`）里哪个能真正编过，挑中了再加到所有目标上，
所以交叉编译和老发行版上不会莫名其妙配置失败。

**可选依赖缺失时会优雅降级**，而不是构建失败：

- 找不到 libcurl → 不定义 `SUBCONV_HAVE_CURL`，只能处理**本地文件**输入，读 URL 会给出提示；
- 找不到 yaml-cpp → 不定义 `SUBCONV_HAVE_YAML`，Clash YAML 作为**输入**不可用（作为输出照常）；
- 找不到 OpenSSL → 不定义 `SUBCONV_HAVE_OPENSSL`，`--probe-cert` 会告警并跳过（Xray 目标退回
  `verifyPeerCertByName`，即"证书与 SNI 对不上"的节点仍会连不上）。

`subconv version` / `GET /api/version` 会打印实际生效的开关。

## 命令行参考

```
subconv 0.1.0 - 订阅转换工具

用法:
  subconv convert -i <文件|URL>... -t <目标> [-o <输出文件>] [选项]
  subconv -i https://example.com/sub -t clash -o config.yaml
  subconv version                        # 显示版本
  subconv --list-targets                 # 列出输出目标
  subconv --list-rulesets                # 列出可选的分流规则集
  subconv --list-dns                     # 列出可选的 DNS 预设
  subconv serve [--port 25500] [--open]  # 启动 HTTP 服务 + Web UI

服务:
  serve                    浏览器打开的图形界面 + subconverter 兼容 HTTP 接口
      --listen <地址>      监听地址，默认 127.0.0.1
                           填 0.0.0.0 会暴露到局域网（接口可让访问者抓取任意 URL）
      --port <端口>        监听端口，默认 25500；0 表示由系统分配空闲端口
      --open               启动后自动打开浏览器
      （抓取类选项 --proxy / --ua / --timeout / --retries / -k / --header /
        --cache-dir / --no-cache 会作为服务端默认值生效）

输入:
  -i, --input <路径|URL>   输入订阅，可重复（多个输入会合并）
                           支持：分享链接列表（含 Base64 包裹）、Clash YAML

输出:
  -t, --target <名称>      输出目标，默认 clash（别名见 --list-targets）
                           clash / xray / singbox  完整配置文件
                           links                   每行一条分享链接（v2rayNG「从剪贴板导入」）
                           base64                  links 列表再整体 base64（当作订阅内容用）
                           v2rayn                  v2rayN / v2rayNG 的 v2rayn:// 分享项，
                                                   唯一能承载 http(s) 代理的形态
  -o, --output <路径>      输出文件，默认写到 stdout
      --no-emoji           分组名不加 emoji
      --no-udp             节点关闭 UDP
      --no-dedup           不做去重
      --tfo                开启 TCP Fast Open
      --sort               按节点名排序
      --clash-legacy       输出原版 Clash 兼容语法
      --no-rules           不输出 rules 段（只生成 proxies/proxy-groups）
      --rulesets <列表>    分流规则集（clash 目标），逗号分隔；见 --list-rulesets
                           默认 local,cn；给空串则只留 MATCH 兜底
      --dns <列表>         dns.nameserver：预设 id 或字面地址（IP / DoH URL），逗号分隔
                           见 --list-dns，默认 cloudflare,google；给空串则不写 nameserver
      --ipv6 / --no-ipv6   根节点与 dns 段的 ipv6，默认开
      --name <订阅名>      写进配置首行注释，并作为下载文件名（自动补扩展名）

抓取:
      --proxy <URL>        http:// 或 socks5:// 代理
      --ua <字符串>        User-Agent，默认 clash-verge/v2.0.0
      --timeout <秒>       单次请求超时，默认 20
      --retries <次数>     失败重试次数，默认 2
  -k, --insecure           跳过 TLS 证书校验
      --header <K: V>      附加请求头，可重复
      --cache-dir <目录>   缓存目录，默认系统临时目录下 subconv-cache
      --cache-ttl <秒>     缓存有效期，默认 300；0 表示永不过期
      --no-cache           禁用缓存

证书:
      --probe-cert         转换前连接每个节点取对端证书的 SHA256 指纹，写进 xray 目标的
                           pinnedPeerCertSha256 / 分享链接的 pcs=
                           为什么需要：Xray 25+ 移除了 allowInsecure，而替代的
                           verifyPeerCertByName 仍要求"链可信 + 名字匹配"，机场那种
                           「证书与 SNI 对不上」的节点会全部握手失败（客户端里全是 -1）
      --probe-cert-timeout <秒>  单个节点的探测超时，默认 5

其他:
  -v, --verbose            打印解析/抓取明细
  -h, --help               显示本帮助
  -V, --version            显示版本
```

`subconv convert` 与 `subconv` 等价（子命令可省），`version` / `--version` / `-V`、`help` / `-h` 同理。
`subconv version` 除了版本号，还会打印已实现 / 规划中的目标、URL 抓取是否可用，以及**实际生效的
TLS CA 路径**（排查 `error adding trust anchors from file` 时先看这一行）。

目标别名：`clash` / `clash.meta` / `meta` / `mihomo` / `clashplus` 指向同一目标；
`v2ray` / `xray-json` 指向 xray；`sing-box` / `sb` 指向 singbox；
`links` / `sharelinks` / `v2rayng-links` 指向分享链接列表；
`base64` / `v2rayng` / `v2rayng-sub` / `v2ray-sub` / `sssub` 指向 base64 订阅；
`v2rayn` / `v2rayn-share` / `v2rayn-full` 指向 v2rayN 分享格式。

## 客户端导入

产物都是标准配置，直接喂给客户端即可。想让客户端**自己定时更新**就用本地服务的 `/sub` 地址。
先在跑 subconv 的机器上启动：

```powershell
.\build\subconv.exe serve --listen 0.0.0.0 --port 25500    # 手机/其它设备要访问就监听 0.0.0.0
```

然后按客户端填：

| 客户端 | 怎么做 |
|---|---|
| Clash Verge / mihomo Party / ClashX Meta | 「订阅」填 `http://<IP>:25500/sub?target=clash&url=<你的订阅>`；或把 `-t clash` 生成的 `.yaml` 直接导入 |
| mihomo / Clash 内核本体 | `-t clash` 出 `config.yaml`，用 `-f config.yaml` 启动（先 `mihomo -t -f config.yaml` 验证） |
| v2rayNG | 「订阅设置」填 `http://<IP>:25500/sub?target=base64&url=<你的订阅>`；临时用可以 `-t links` 后从剪贴板导入 |
| v2rayN | 同上填订阅地址；含 http 节点的订阅请把 `target` 换成 `v2rayn`（见下节） |
| Shadowrocket | 用 `-t links` / `-t base64` 的产物，或 `/sub?target=base64&...` |
| sing-box 官方客户端 | `-t singbox` 出 `.json` 直接导入（sing-box 校验严格，未知字段会直接报错） |

> `--listen 0.0.0.0` 会暴露到局域网，而 `/sub` 允许调用方让服务端去抓取任意 URL（SSRF 面），
> 请只在可信网络里这么用。

**订阅里是 xhttp（2025 年底之后的机场基本都是）时，选目标就一条规则**：

- 客户端是 mihomo 系（Clash Verge / mihomo Party / ClashX Meta）→ 用 `clash` 目标，
  并确认内核 **≥ v1.19.x**（`network: xhttp` 是那时才有的传输；老内核会当 TCP 用，表现是全部 -1）。
- 客户端是 Xray 系（v2rayN / v2rayNG / Shadowrocket 的 Xray 内核）→ 用 `xray` / `links` / `base64`，
  **必须加 `--probe-cert`**（HTTP 侧加 `probe_cert=true`），否则机场"证书与 SNI 对不上"的节点
  会全部握手失败 —— 详见「[证书校验](#证书校验为什么同一份订阅在-clash-里能用在-xray-里全是--1)」。
- 客户端是 sing-box → 这份订阅用不了：sing-box 没有 xhttp 传输，节点会被跳过（只剩告警）。

Clash 侧有两个前提要知道：

- `GEOSITE` 规则需要客户端的 `geosite.dat`（mihomo 首次加载会自动下载；Clash Verge / mihomo Party
  一般自带），`GEOIP` 需要 `geoip.metadb`。离线环境请手动放置，或改用不含 GEOSITE 的规则集。
- Clash 目标生成的是 **meta 语法**；给原版 Clash 用要加 `--clash-legacy`，此时
  vless / hysteria2 / tuic / ssr / snell 节点会被跳过并写进配置头部注释。

## 分享链接输出（v2rayNG）

`links` 与 `base64` 两个目标把节点反向导出成**分享链接**，供 v2rayNG / v2rayN / Shadowrocket
等客户端导入（v2rayNG 的订阅内容就是「链接列表的 base64」）：

```powershell
.\build\subconv.exe -i upstream.yaml -t links -o nodes.txt        # 每行一条链接
.\build\subconv.exe -i upstream.yaml -t base64                     # 直接给客户端当订阅内容
.\build\subconv.exe -i upstream.yaml -t links -o nodes.txt --probe-cert   # 带证书指纹（xhttp 订阅必加）
```

格式对齐 v2rayNG 自身的实现（`com.v2ray.ang.fmt.*Fmt`，逐字段核对过源码）：

| 协议 | 产出形态 |
|---|---|
| shadowsocks | `ss://base64url(method:password)@host:port?plugin=...#名称`（SIP002；插件串原样回写） |
| vmess | `vmess://base64(JSON)`，18 个键、值全为字符串、键序固定 |
| vless | `vless://uuid@host:port?encryption=<VLESS 加密串\|none>&security=tls\|reality&…`（reality 带 `pbk`/`sid`/`spx`；<br>xhttp 带 `type=xhttp&mode=…&extra=<JSON>`；`--probe-cert` 额外带 `pcs=<证书指纹>`） |
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
一个都不选也是合法配置（只剩 `MATCH` 兜底）。GEOIP 类规则会自动带 `no-resolve`，避免为了匹配 IP
规则而多做一次 DNS 解析。

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
v2rayNG 订阅 / v2rayNG 完整），勾选项（emoji、UDP、去重、rules、排序、TFO、IPv6、
**探测证书指纹**、原版 Clash 语法），
配置代理 / UA / 超时 / 重试，转换后直接复制或下载；界面还会给出可填进客户端的 `/sub` 地址，
并在链接类目标下显示「链接 N 条」。勾上「输出 rules」会展开**分流规则集复选**，「高级选项」里
还有 **DNS 复选 + 自定义地址**，这些选择都会记在浏览器里、也会写进生成的 `/sub` 链接。
选「v2rayNG 订阅」时，把生成的那个 `/sub?target=base64&url=…` 地址填进 v2rayNG 的「订阅设置」
就能自动更新。

输入框的切分规则：**独占一行、且顶格**的 `http(s)://…` 行当订阅链接去抓取，其余行**连缩进原样**
合并成订阅内容。所以整段粘贴 Clash YAML / JSON 版 Clash 配置不会被误判成链接，也不会丢缩进
（早期版本把每行 `trim` 之后再拼回去，YAML 结构一丢就报 `end of map not found` —— 见
「[FAQ](#故障排查faq)」，`node tools/check-web-input.mjs` 守住这个回归）。

界面是**内嵌进二进制**的：改完 `data/web/index.html` 要重新构建（CMake 会重新内嵌），而且
**正在跑的 `serve` 得重启**才会换成新界面 —— 没有热更新，别对着旧页面找 bug。

HTTP 接口是 subconverter 的兼容子集：

| 方法与路径 | 说明 |
|---|---|
| `GET /` | Web UI |
| `GET /api/version`（`/version` 亦可） | 版本、已实现 / 规划中的目标、libcurl 是否可用 |
| `GET /api/rulesets` | 分流规则集目录（id / 名称 / 策略 / 说明 + 默认选择） |
| `GET /api/dns` | DNS 预设目录（id / 名称 / 区域 / IPv4 / IPv6 + 默认选择） |
| `POST /api/convert` | JSON 进 JSON 出：`{target, filename, sources[], content, options{}, fetch{}}` |
| `GET /sub?target=&url=` | 返回配置文本；`url` 可用 `\|` 分隔多个，也可重复出现（`url` / `urls` / `s` 等价） |
| `GET /clash`、`GET /xray`、`GET /sing-box` | 同上，路径即目标 |

`/sub` 支持的参数：

| 参数 | 说明 |
|---|---|
| `target` | 目标，默认 `clash` |
| `url` / `urls` / `s` | 订阅来源，可重复、可用 `\|` 分隔 |
| `content` | 直接内联订阅内容（与 `url` 二选一或并用） |
| `filename` | 订阅名，用于下载文件名与配置首行注释 |
| `emoji` `udp` `tfo` `sort` `dedup` `rules` `clash_legacy`(=`legacy`) `ipv6` `probe_cert` | 布尔开关，**键出现即覆盖默认值**，写 `=false` 可关闭 |
| `probe_cert_timeout` | `probe_cert` 的单节点探测超时（秒），默认 5 |
| `rulesets` `dns` | 逗号分隔列表，见下表 |
| `proxy` `ua` `timeout` `retries` | 抓取参数 |
| `insecure` | 跳过 TLS 证书校验 |
| `cache_dir` `cache_ttl` `no_cache` | 缓存控制 |

上游带 `subscription-userinfo` 时，响应头会把它原样透出。

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

- **内容嗅探**：自动区分「分享链接列表 / 整体 Base64 / Clash YAML（含 JSON 版）/ JSON 配置 / 网页」。
  机场返回 404 网页或鉴权页时给出明确提示，而不是抛出一堆 YAML 语法错；JSON 版 Clash 配置按其中的
  `proxies` / `proxy-groups` / `mixed-port` 等键识别出来，交给 Clash 解析器而不是当成「不支持的 JSON 配置」。
- **磁盘缓存**：默认 `%TEMP%\subconv-cache`，同一订阅在 TTL 内不重复请求。
- **离线降级**：网络失败时自动回落到任意龄期的本地缓存，并在告警里说明。
- **`subscription-userinfo`**：解析流量与到期信息并汇总输出到 stderr，同时作为响应头透传给客户端。

## 故障排查（FAQ）

**输出的节点比订阅里少（甚至只剩几条）** —— 逐层排查：

1. **目标内核不支持**：按「[支持矩阵](#协议--目标-支持矩阵)」，Xray 会跳过 hysteria/hysteria2/tuic/ssr/snell，
   sing-box 会跳过 ssr/snell，原版 Clash 语法（`--clash-legacy`）还会跳过 vless。加 `-v` 看告警。
2. **`links` / `base64` 装不下 http 代理**：v2rayNG 没有 `http://` 解析器，所以一份
   「16 http + 2 ss + 1 vmess」的 19 节点订阅用 `links` 只剩 3 条 —— 这时用 `-t v2rayn`，19 条都在。
3. **去重**：同服务器同协议同凭据的节点会被合并，加 `--no-dedup` 可关（但节点名重复在
   v2rayNG 里会被静默丢弃，所以不建议）。

**转出来的节点在客户端里"全部 -1"（Clash 能用、Xray / v2rayN 全超时）** —— 先分清是哪一层：

1. **客户端内核不支持 xhttp**：这已经是 2025 年底之后机场的主流传输。mihomo 需要 **v1.19.x 以上**
   （`transport/xhttp` 只接在 vless 出站上），Xray 需要 25.x 以上；老内核会把 `network: xhttp`
   当 tcp 用 → 全部连不上。先用 `mihomo -v` / `xray version` 确认版本。
2. **证书校验**：机场普遍 `skip-cert-verify: true` + 一个与证书无关的 SNI。mihomo 有
   `skip-cert-verify` 所以没事，但 **Xray 25+ 移除了 `allowInsecure`**，只留
   `pinnedPeerCertSha256`（指纹，等价于跳过校验）和 `verifyPeerCertByName`（仍要链可信 + 名字
   匹配，这种节点必然失败）。这是「全是 -1」最常见的原因，加 `--probe-cert` 重新转即可，
   详见「[证书校验](#证书校验为什么同一份订阅在-clash-里能用在-xray-里全是--1)」。
3. **节点本身是死的**：机场常塞占位/超时节点（`server: 0.0.0.0`、上游 504）。这类节点转换
   如实保留，是订阅的问题，不是转换的问题 —— 用 mihomo 的 `/proxies/<name>/delay` 逐个测
   就能看出哪些是真的活节点。
4. **sing-box 目标节点特别少**：sing-box 没有 xhttp 传输（`sing-box check` 会报
   `unknown transport type: xhttp`），xhttp 节点会被跳过并告警 —— 这种订阅请用 `-t clash` / `-t xray`。
5. **中间层吃掉了 `encryption`**：节点是 VLESS Encryption（`encryption=mlkem768x25519plus.…`）
   时，**丢了这个参数不会有任何报错** —— 服务端解不开 VLESS 头部，既不回包也不关连接，客户端
   只能等到 dial timeout。特征是「同一份节点直接给 mihomo 能用，过一次订阅转换就全超时」。
   查两处：产物里 vless 节点有没有 `encryption:`（或链接里的 `encryption=`），以及日志里有没有
   `vision: not a valid supported TLS connection`（mihomo 在「带 `flow: xtls-rprx-vision` 但没有
   encryption、又不是 tcp+TLS」时的报错）—— 详见
   「[VLESS Encryption](#vless-encryption为什么节点连不上却没有任何报错)」。

**粘贴 Clash YAML 报 `yaml-cpp: error at line 77, column 1: end of map not found`** —— 先看**缩进**：
YAML 靠行首空格表达层级，内容一旦被整段去掉缩进（自己写脚本 `trim` 每行再拼接、过一遍会吃掉行首
空白的编辑器/终端），解析器就会在「下一个节点开始」的那一行炸掉 —— 报的行号往往正好是第二个
`- name:` / `- alpn:`。subconv 现在会把**出问题的行号 + 那一行原文**一起打出来，对着看即可；
Web UI 的输入框也已改成保留缩进（`node tools/check-web-input.mjs` 守着这个回归）。
如果是从编辑器里复制出来的，用 `-i 文件路径` 直接读原文件最稳。

**`mihomo -t` 报 `list xxx not found in geosite.dat`**：配置里用了 `GEOSITE`，但客户端没有
`geosite.dat`。mihomo 首次加载会自动下载；离线/下载失败时请手动放置，或只选
`--rulesets local`（纯 GEOIP，不依赖 geosite.dat）。

**HTTPS 抓取报 `error adding trust anchors from file`**：MSYS2 的 `ucrt64` 偶尔会留下 **0 字节**的
`etc/ssl/certs/ca-bundle.crt`，而 libcurl 的内置默认路径正指向它。subconv 会自动退回到
`<MSYS2>/usr/ssl/certs/ca-bundle.crt` 等可用位置，`subconv version` 会打印实际生效的路径；
也可用环境变量 `CURL_CA_BUNDLE` 指定，或用 `-k` 跳过校验。

**控制台中文乱码**：程序内部一律 UTF-8，写给人看的提示会先转成控制台代码页（Windows 默认 936/GBK）。
若调用方按别的编码读子进程输出（例如 PowerShell 7 固定按 UTF-8 解码），用
`SUBCONV_CONSOLE_ENCODING=utf-8`（或 `936`、`auto`，默认 `auto`）显式指定。
注意配置**载荷**始终写原始 UTF-8 字节，它要落盘或交给内核，不受这个开关影响。

**配置里没写 `nameserver`**：`--dns ""` 或 `dns=` 是「一个都不要」的意思。另外拼错的预设 id 不会
被当成解析器写进去（会让 mihomo 拒绝加载整份配置），而是给一条告警并跳过。

**订阅名 / 中文路径**：订阅名里的非法字符会被剔掉；命令行传中文路径依赖 ACP(GBK) 行为，
如果在 UTF-8 终端（如 MSYS2 的 bash）里调用，路径请用 Windows 形式或改用界面 / HTTP 接口。

**抓到的像是机场错误页**：查看 `-v` 输出与嗅探提示；常见原因是链接失效、需要鉴权、或
User-Agent 被拦（`--ua` 换成对应客户端的 UA），临时可加 `--no-cache` 排除缓存干扰。

## 内核校验

本节全部在**本地**跑（`tools\validate.ps1`；CI 不跑，原因见「[CI 与发布](#ci-与发布github-actions)」）。

| 目标 | 内核 | 版本 | 结果 |
|---|---|---|---|
| clash | mihomo | v1.19.30 | `test is successful` |
| xray | Xray-core | v26.9.9 | `Configuration OK.` |
| singbox | sing-box | v1.14.0 | exit 0 |

单元测试：**937 项断言全部通过**（含 HTTP 请求映射、转换核心、控制台编码、CA bundle 不变式、
xhttp 的解析/输出/往返与证书指纹、VLESS Encryption 的块校验与四目标透传、
JSON 版 Clash 配置的嗅探与解析、去缩进 YAML 的报错信息）。

除内置夹具（19 节点，含 `vless` + `network: xhttp` + `download-settings`）外，还用真实机场
订阅（一元机场，2026-09 的 Clash 订阅：18 个 `vless` + `xhttp` 节点 + 2 个占位节点）做了端到端
验证，**不只是"能加载"，而是真的能通**：

| 验证 | 命令 | 结果 |
|---|---|---|
| 产物可加载 | `validate.ps1 -Source sub.txt -Target clash/xray/singbox -ProbeCert` | mihomo `test is successful` / Xray `Configuration OK.` / sing-box exit 0 |
| clash 节点可用 | mihomo 加载产物后逐个查 `/proxies/<name>/delay` | 18 个活节点里 14 个返回真实延迟（1.3–3.6 s），3 个 JP 节点上游 504 |
| xray 节点可用 | `xray run` + `curl --socks5-hostname 127.0.0.1:11808 http://www.gstatic.com/generate_204` | 连续 3 次 `HTTP/1.1 204 No Content` |
| v2rayN/v2rayNG 可导入 | `verify-v2rayn.ps1 -Path out-v2rayn.txt` | 19 行全部可导入，18 个 xhttp 节点带 `XhttpMode`/`XhttpExtra` |

另一份输入来自面板的 `?app=clash` 接口（BPB，**JSON 版 Clash 配置**，8 节点：4 `vless` + 4 `trojan`，
全是 `network: ws`），既用它验证了「JSON 方言当 Clash 输入」，也验证了 Web UI 的粘贴路径
（同一份内容用 `POST /api/convert` 与 `GET /sub?content=` 都是 8 节点）：

| 验证 | 命令 | 结果 |
|---|---|---|
| JSON 版 Clash 当输入 | `subconv <board-url>?app=clash -t clash` | 解析 8 节点，`ws-opts` / `servername` 与原配置一致 |
| 三目标内核校验 | `validate.ps1 -Source bpb-clash.json -Target clash/xray/singbox` | mihomo `test is successful` / Xray `Configuration OK.` / sing-box exit 0 |

> 关于 `-ProbeCert`：不加它时 xray 产物里的节点会因为「证书与 SNI 对不上」**全部握手失败**
> （`peer cert is invalid (against root CAs and verifyPeerCertByName)`，客户端里就是全是 `-1`），
> 详见「[证书校验](#证书校验为什么同一份订阅在-clash-里能用在-xray-里全是--1)」。

### VLESS Encryption 的端到端校验（2026-09）

单个真实节点：`encryption=mlkem768x25519plus.native.0rtt.<1610 字符认证公钥>` +
`type=ws` + `security=tls` + `flow=xtls-rprx-vision`，服务器是 Cloudflare tunnel 域名。
四个目标产物里落地的 encryption **与源串逐字节相同**（1610/1610）：

| 目标 | 落点 | 一致性 |
|---|---|---|
| clash | `proxies[].encryption` | ✅ 1610 字节 |
| xray | `outbounds[].settings.vnext[0].users[0].encryption` | ✅ 1610 字节（`flow` 同时保留） |
| links | query 的 `encryption=` | ✅ 1610 字节 |
| v2rayn | `ProtoExtraObj.VlessEncryption` | ✅ 1610 字节 |

内核侧对照实验（mihomo **v1.19.31** / Xray **26.3.27** / sing-box **1.14.1**）：

| 场景 | 结果 |
|---|---|
| `encryption` + `flow` 都在 | `HTTP/1.1 204 No Content`，出口 `151.241.88.97`（ByteVirt LLC，US Salt Lake City），~5.6–7.3 Mbps |
| 去掉 `encryption` | **一句报错都没有**：服务端解不开 VLESS 头部，既不回包也不关连接，客户端一路等到 dial timeout |
| 保留 `flow`、去掉 `encryption` | mihomo 直接拒绝该节点：`vision: not a valid supported TLS connection` |
| sing-box 1.14.1 | 内核二进制里搜不到 `mlkem768x25519plus`，node 被跳过并告警（不是产出加载不了的配置） |

这正是这段代码存在的理由：节点直连 mihomo 能用，过一层订阅转换就全部超时，而中间那层此前把
没见过的 `encryption` 参数丢掉了 —— 丢一个内核扩展字段不会报错，只会让所有节点变成「死的」。
详见「[VLESS Encryption](#vless-encryption为什么节点连不上却没有任何报错)」。

## 开发与测试

```powershell
.\build.ps1 -Test                                  # 构建 + 跑单元测试（Windows 上 937 断言）
.\build\subconv_tests.exe                          # 只跑测试（需已构建）
node tools\check-web-input.mjs                     # 校验 Web UI 输入框切分（CI 也会跑）

.\tests\fixtures\generate.ps1                      # 生成夹具：19 节点 / 11 种协议 + xhttp
.\tools\validate.ps1 -Setup                        # 下载三个内核到 tools\bin\（不入库）
.\tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target clash|xray|singbox
.\tools\validate.ps1 -Source sub.yaml -Target xray -ProbeCert   # 带证书指纹（联网）
.\tools\verify-v2rayn.ps1 -Path out-v2rayn.txt -LegacyNoSegment
```

Linux / Termux 上是同一套，换成：

```bash
./build.sh --test                                  # 等价于 .\build.ps1 -Test
./build/subconv_tests
node tools/check-web-input.mjs
```

**改完工作流想先自查**：`actionlint .github/workflows/build.yml`。
（早先用过 `msys2/setup-msys2` 和 `windows-11-arm`，那时要靠 `.github/actionlint.yaml` 给 actionlint
补上它不认识的 runner 标签；现在 Windows 改成在 Linux 上交叉编译，一个自定义标签都不剩，那个文件也删了。）

要点：

- **加一个协议**：`src/parse/` 下加解析器 → `Protocol` 枚举与 `to_string`（`src/core/types.cpp`）
  → 各 `src/emit/*.cpp` 的 `switch` 分支（不支持的协议返回空并 push 告警）→ 更新本文档的支持矩阵
  → 在 `tests/test_main.cpp` 加断言。漏掉 emit 分支会在编译期报 `-Wswitch`（工程开着 `-Wall -Wextra`）。
- **加规则集 / DNS 预设**：只改 `src/emit/rulesets.cpp` / `src/emit/dns.cpp` 的目录表。
  CLI（`--list-*`）、HTTP（`GET /api/*`）、Web UI 的复选都会自动跟着变 —— 这是刻意的单一真源设计。
- **改 Web UI**：编辑 `data/web/index.html`，CMake 在配置阶段把它读成 C++ 原始字符串写进
  `build/generated/web_ui.hpp`（已设 `CMAKE_CONFIGURE_DEPENDS`，改文件会触发重新配置）。
  动了输入框切分（`splitInput`）就跑一下 `node tools\check-web-input.mjs`：YAML / JSON 靠缩进，
  这里踩过一次「每行 trim 后拼回去」的坑，代价是粘贴的配置全部解析失败。
- **编码约束**（踩过坑，别绕过）：
  - 仓库内所有 `.ps1` 必须 **UTF-8 带 BOM**，否则 PowerShell 5.1 按 ANSI 读取中文会导致解析失败；
    `build.ps1` 会在构建前拦截。
  - 内部字符串一律 UTF-8；给人看的输出走 `console::write*` 做代码页转换；配置载荷写原始 UTF-8。
  - 代码里**刻意不用 `std::filesystem`**：Windows 上它把窄字符串按 UTF-8 转换，而 MinGW 的 `argv`
    是 ANSI(GBK)，中文路径会抛 `Illegal byte sequence` 直接崩溃。统一用 CRT 的 `_access` / `_mkdir`
    （见 `src/core/fsutil.cpp`），与 `std::ofstream` 的编码行为保持一致。
- **行尾**：`.gitattributes` 钉住 LF（本机全局 `core.autocrlf=true`，不钉住检出会变 CRLF）。
  `build/`、`tools/bin/`、`tests/fixtures/out_*`、`cache/` 已在 `.gitignore` 里。

## CI 与发布（GitHub Actions）

工作流：[`.github/workflows/build.yml`](.github/workflows/build.yml)。先由 `version` job 算出这次的版本号，
然后四个平台组**并行**编译（每组是矩阵，`fail-fast: false`，一个架构挂了不影响其它架构），
最后由 `release` job 汇总所有产物、生成 `SHA256SUMS.txt` 并发 Release：

| job | 跑在哪 | 做什么 |
|---|---|---|
| `version` | `ubuntu-22.04` | 读全部 tag 算出这次的版本号（规则见下） |
| `linux-native` | `ubuntu-22.04` / `ubuntu-22.04-arm` | x86_64、aarch64 原生构建 + 单元测试 + 打包 |
| `linux-qemu` | `ubuntu-22.04` + QEMU | armv7l / i686 在 `debian:bookworm` 容器里、riscv64 在 `ubuntu:24.04` 容器里构建 + 测试 + 打包 |
| `windows` | `ubuntu-22.04` | 用 llvm-mingw 交叉编译 x86_64 / aarch64：静态 OpenSSL + libcurl 现编并缓存 + 编译 + 架构断言 + 打 zip（**不运行产物**） |
| `termux` | `ubuntu-22.04` + QEMU | aarch64 / arm 两个真·手机架构：在 `termux/termux-docker` 里 `pkg install` 后构建 + 测试 + 打包（386/amd64 镜像已按需去掉） |
| `release` | `ubuntu-22.04` | 下载全部 `subconv-*` 产物 → `sha256sum` → 建 tag + 发 Release（`contents: write`） |

几点设计取舍：

- **Windows 也走交叉编译，而且依赖自己编**：Debian/Ubuntu 没有给 mingw 预编译的 libcurl / OpenSSL
  （Arch 之类也不带），所以 `tools/build-windows-deps.sh` 用 llvm-mingw 现编两个静态库。
  `actions/cache` 只缓存**工具链 + 依赖安装前缀**（`llvm-mingw` + `win-deps`），key 是
  「三元组 + 各版本号 + `tools/build-windows-deps.sh` / `tools/mingw-w64-toolchain.cmake` 的哈希」——
  带上脚本哈希是为了改了编译选项就会自动重编，不会把旧配置的产物当新的用；源码目录
  （`win-deps-src`，解压加编译后好几个 GB）不进缓存：命中时用不到，没命中时 CI 直连 GitHub 也很快。
  用 llvm-mingw 而不是发行版的 mingw-w64，是因为**一份工具链同时覆盖 x86_64 和 aarch64**
  （Ubuntu 根本没有 aarch64 的 mingw），而且它默认就是 UCRT + libc++。
- **Linux 的"异架构"不手工交叉编译，而是 QEMU + Docker**：在容器里 `apt install` 就是目标架构的
  libcurl / openssl，比自己搭一份交叉 sysroot 简单可靠得多，代价是慢。基础镜像按架构挑（原因见
  「[支持的平台](#支持的平台)」），基线是 Debian 12 的 glibc 2.36 / Ubuntu 24.04 的 2.39。
- **Linux 包一律 `-DSUBCONV_VENDOR_YAMLCPP=ON -DSUBCONV_STATIC_RUNTIME=ON`**：把 yaml-cpp 静态编进去，
  避开 0.7/0.8 的 ABI 分叉（原因见「[支持的平台](#支持的平台)」）。
- **`release` 只要有一个平台成功就会发**（`if: always() && … || …`），因为 QEMU / Termux 这些模拟任务
  偶尔会因为超时或上游源抖动失败，不该把已经编好的十几个平台一起拖住；缺了哪些平台会在日志和
  Step Summary 里显式列出来，不会让人误以为"全平台都发了"。
- **PR 只跑两组任务**（linux x86_64 + aarch64、windows x86_64 + aarch64），全平台矩阵只在 push 到 `main`、
  打 tag 和手动触发时跑 —— 否则一个 PR 要占用十几台机器几十分钟。实现方式是给重任务加
  `if: github.event_name != 'pull_request'`（job 级的 `if` 里没有 `matrix` 上下文，不能按架构挑条目）。
- **arm64 的 Linux runner 是 GitHub 托管的新机型**（`ubuntu-22.04-arm`）：公开仓库免费，
  私有仓库/组织可能还没开。真用不了的话，把矩阵里那一行删掉即可，其余平台不受影响。
  （Windows 一侧改成 Linux 交叉编译之后，**不再需要任何 Windows runner**，`windows-11-arm`
  这个机型也就不依赖了。）
- **Termux 不能用 `docker run -e VAR` 传变量**：`termux/termux-docker` 的 entrypoint 在 root 身份下
  会走 `su ... env -i` 把环境清空，只回填白名单里的 `ANDROID_DATA` / `HOME` / `PATH` / `PREFIX` /
  `TMPDIR` 等几个，自己 `-e` 进去的变量在容器里根本不存在（脚本里一引用就是
  `SUBCONV_VERSION: unbound variable`）。现在改为把值当**位置参数**递给容器的 `bash`，利用 entrypoint
  末尾的 `"$@"` 原样透传。
- **aarch64 的 OpenSSL 复用内置的 `mingw64` 目标**：OpenSSL 3.5 的 `Configurations/10-main.conf`
  里只有 `mingw` / `mingw64`，**没有 Windows/ARM64**（`arm-xlate.pl` 倒是认 `win64` 那套 perlasm）。
  实测 `mingw64` 这套配置对 `aarch64-w64-mingw32-clang` 完全可用，所以没有另外造目标：
  `bn_ops=SIXTY_FOUR_BIT` 对 Windows/ARM64 同样成立（Windows 是 LLP64）；它加的 `-m64`
  在 aarch64 上无告警、照样产出 `coff-arm64`；它写的 `asm_arch=x86_64` 只在启用汇编时才读，
  而我们是 `no-asm`；目标名以 `mingw` 开头这一点还很重要 —— `Configure` 只对
  `^(Cygwin|mingw|VC-|BC-)` 开 `winstore`，名字带 `mingw` 前缀 Windows 证书库后端才不会被静默关掉。
- **两个架构的 OpenSSL 都加 `no-asm`**：llvm-mingw 里没有 GNU `as`，x86_64 的 perlasm 汇编还得额外
  把 `AS` 指到 clang 才编得动，aarch64 更没有能用的汇编目标。这个程序只做几次 TLS 握手，纯 C 的
  加密实现完全够用，换来的是这套构建不依赖任何汇编器。
- **两个架构的 OpenSSL 都加 `no-module`**：`no-shared` 只管 `libcrypto` / `libssl` 两个动态库，
  `providers/legacy.dll` 这种「可动态加载模块」照样会编 —— 产物是个单文件静态 exe，根本用不到它
  （默认 provider 是静态进 `libcrypto.a` 的）。它还会让 aarch64 直接编失败：资源对象是按
  `mingw64` 配置里的 `shared_rcflag`（`--target=pe-x86-64`）编出来的，链接时报
  `machine type x64 conflicts with arm64`。
- **CI 里会断言 exe 的架构**（`llvm-objdump -f` 必须是 `coff-x86-64` / `coff-arm64`），免得哪天三元组或
  工具链串了，把 x86_64 的产物当 aarch64 发出去。这里特意用不带前缀的 `llvm-objdump`，而不是
  llvm-mingw 里 `<三元组>-objdump` 那几个包装器：包装器（`objdump-wrapper.sh`）会把格式名改写成
  libtool 认识的老名字 —— `COFF-x86-64` → `pe-x86-64`、`COFF-ARM*` → `pe-arm-wince`，连它自己的注释
  都写着「This is wrong; ... arm64 definitely isn't」，拿它断言架构等于没断言。
- **Windows 产物在 CI 里不运行**：装 Wine 那一套（`wine64` + ARM64 根本跑不了）已经去掉了，
  两个架构都只保证「编得出来 + 链接通过 + 架构断言正确」；要在 Windows 上真跑单元测试，
  用 MSYS2 本地构建（`build.ps1`）或在 Windows 机器上执行 `subconv_tests.exe`。

> **内核校验不在 CI 里跑**（曾经跑过，已移除）：那一步要调 `tools\validate.ps1`（PowerShell 脚本），
> 而 Windows 任务现在是 Linux 上的交叉编译，没有 PowerShell 可用。好在产物已经变成**单个静态 exe**
> （libcurl / OpenSSL / libc++ 全在 exe 里），不会再出现早先那种「缺 DLL 导致进程直接起不来、
> 还没有任何输出（exit `0xC000007B`）」的坑 —— 那种故障最费 CI 时间。**本地仍是必跑项**，
> 见「[内核校验](#内核校验)」：产物要能被真实内核加载，靠的是每次改完 emit 后本地跑一遍
> `tools\validate.ps1`（在 Windows 上执行 `subconv.exe` / `subconv_tests.exe`）。

### 四种触发方式

| 触发 | 行为 |
|---|---|
| push `main` | 全平台编译 + 测试 + 上传 artifact，**不发 Release**（版本号是 `dev-<commit>`） |
| pull request | 只跑 `linux-native`（x86_64 / aarch64）和 `windows`（x86_64 + aarch64 交叉编译）两组，**不发 Release** |
| push `v*` 或 `[0-9]*` 标签 | 全平台编译，并以该标签发 Release |
| **手动 `workflow_dispatch`** | **自动递增版本号并存 Release**（`tag` 留空即可；填了就用手填的） |

手动发版的版本号规则：**次版本只到 9，满了就把主版本 +1、次版本归 0** ——

```
（尚无 tag）→ v1.0 → v1.1 → … → v1.9 → v2.0 → v2.1 → … → v2.9 → v3.0 → …
```

实现是读仓库里所有形如 `1.0` / `1.9` / `v2.0` 的标签取最大者再 +1（非版本标签如 `release` 会被忽略，
`v` 前缀可有可无，但**新算出来的 tag 一律带 `v`**）；`workflow_dispatch` 还支持 `bump: major` 直接把
主版本 +1。逻辑见工作流里的 `version` job。自动算出的 tag 由 `softprops/action-gh-release` 创建，
指向本次构建的 commit。

怎么手动发一版：仓库 **Actions → build → Run workflow**，`tag` 留空 → 跑完就多一个 Release。

> 首次发版若报 `Resource not accessible by integration`，说明仓库的默认 token 权限是只读：
> 去 **Settings → Actions → General → Workflow permissions** 选 **Read and write permissions**
> （`release` job 要建 tag、发 Release，工作流里已经写了 `permissions: contents: write`）。

### 产物

一次发布最多挂 9 个包 + `SHA256SUMS.txt`，完整清单见「[支持的平台](#支持的平台)」。两类形态：

**Windows（`subconv-<tag>-windows-<arch>.zip`）**，解压即可用：

- `subconv.exe` —— **静态链接**：libcurl / OpenSSL / libc++ 全在 exe 里，不需要任何配套 DLL，
  目标机也不用装运行库
- `README.md`
- `HOW-TO-RUN.txt`

**Linux / Termux（`subconv-<tag>-<平台>.tar.gz`）**：

```
subconv-v1.0-linux-x86_64/
├─ subconv            可执行文件（已内置静态 yaml-cpp）
├─ README.md
└─ HOW-TO-RUN.txt     怎么跑、需要装哪些运行库
```

构建时用 `-DSUBCONV_VERSION:STRING=<tag>` 把版本号注入二进制（CMake 缓存变量，见 `CMakeLists.txt`），
所以 `subconv version` / 配置首行注释 / `GET /api/version` 里的版本和 Release 标签一致。

> Windows 产物由 `tools/package-windows.sh` 打包（Linux 侧脚本，和 Linux 包共用 `dist/` 命名习惯）；
> Linux / Termux 一侧用的是 `./build.sh` + `tools/package.sh`。
> `tools/validate.ps1` 的 curl 路径与代理也是可移植的：curl 优先取 `SUBCONV_CURL` / MSYS2 安装位置 / PATH，
> 代理只在 `SUBCONV_VALIDATE_PROXY` 给了、或本机 10808 真在监听时才走。


## 目录结构

```
订阅转换/
├─ CMakeLists.txt            构建定义（同时把 Web UI 内嵌成 web_ui.hpp）
├─ build.ps1                 Windows 本地构建脚本（MSYS2 原生，-Test / -Clean / -Config / -BuildDir）
├─ build.sh                  Linux / Termux 构建脚本（--test / --clean / --vendor-yaml / --prefix）
├─ .github/workflows/build.yml  多平台矩阵构建 + 自动版本号 + 发布
├─ include/subconv/          公共头（types / error / codec / yaml / json / fsutil / convert / fetch / server / console / vless_encryption）
├─ src/
│  ├─ core/                  数据模型、文件与编码工具（console.cpp：控制台代码页适配；
│  │                        vless_encryption.cpp：VLESS Encryption 块的校验与规范化）
│  ├─ codec/                 Base64、percent-encoding、URI、字符串工具
│  ├─ parse/                 各协议分享链接 → ProxyNode（uri_common 公共参数映射、clash_yaml 上游配置输入）
│  ├─ fetch/                 libcurl 抓取、内容嗅探、磁盘缓存、离线降级
│  │                        certprobe.cpp：--probe-cert 的 OpenSSL 证书指纹探测（可选依赖）
│  ├─ emit/                  渲染器：clash / xray / singbox / 分享链接 / 规则集 / DNS
│  │                        xhttp.cpp：XHTTP 在 mihomo / Xray / 分享链接三种形态间的公共换算
│  ├─ server/                HTTP 服务 + Web UI（request 映射 / convert 核心 / http 传输三层分离）
│  └─ cli/                   main（参数解析与终端输出）
├─ data/                     内置资源（详见 data/README.md）
│  └─ web/index.html         Web UI 源文件；构建时由 CMake 内嵌进二进制
├─ tests/                    单元测试 + fixtures（generate.ps1 生成）
├─ third_party/nlohmann/     vendor 的 nlohmann/json
└─ tools/                    开发期、交叉编译与打包脚本（bin/ 不入库）
   ├─ mingw-w64-toolchain.cmake  Windows 交叉编译用的 CMake 工具链文件（llvm-mingw）
   ├─ setup-llvm-mingw.sh    下载/展开 llvm-mingw（UCRT，一份含 x86_64 + aarch64）
   ├─ build-windows-deps.sh  交叉编静态 OpenSSL + libcurl（含 aarch64 的 OpenSSL 目标补丁）
   ├─ package.sh             把构建产物打成发行包（Linux / Termux 用）
   ├─ package-windows.sh     把 exe 打成 zip（Windows 用）
   ├─ validate.ps1           用真实内核校验产物
   └─ verify-v2rayn.ps1      重放 v2rayN / v2rayNG 的导入逻辑
```

## 设计要点

- **中间模型统一**：所有协议先解析成 `ProxyNode`，再由各目标渲染器映射，避免字符串拼接。
- **按内核能力裁剪**：Xray 不支持 hysteria2/tuic/ssr/snell，sing-box 不支持 ssr/snell，
  转换时按目标跳过并透出告警，而不是生成加载不了的配置。
- **不认识的扩展字段也不丢**：VLESS 的 `encryption`、XHTTP 的 `extra` 这类内核扩展参数会原样
  透传到各目标，校验层只告警、不改写（转换器丢一个内核扩展字段，客户端往往表现为「全部超时」
  而不是报错，没线索可查）。
- **手写 YAML 输出器**：Clash 配置对 key 顺序与引号敏感，手写才能保证输出确定、可读、可校验。
- **手写 Base64 编解码**：订阅场景里 Base64 变体极多（URL-safe、缺 padding、二次包裹、含空白），
  必须自行掌控容错策略。
- **JSON 走 `ordered_json`**：Xray / sing-box 配置字段顺序稳定，diff 友好。
- **规则集只用内置地理数据**：不依赖远程 `rule-providers`（那会让 mihomo 启动时必须联网下载，
  失败就整份配置起不来），因此产物离线可用、可 `mihomo -t` 全量验证；目录内建在
  `src/emit/rulesets.cpp` / `src/emit/dns.cpp`，由 `--list-*` 与 `GET /api/*` 共用。
- **HTTP 服务分三层**：`request.cpp` 只做参数映射、`convert.cpp` 是纯转换核心、`http.cpp` 管传输，
  所以转换核心能被单元测试直接调用，不必起服务器。
- **控制台输出跟着代码页走**：程序内部字符串一律 UTF-8，但写给人看的提示在 Windows 上会先
  转成控制台代码页（`src/core/console.cpp`）。Windows 控制台默认是 936(GBK)，硬写 UTF-8 中文
  就会显示成「璁㈤槀閾炬帴」；而 PowerShell / cmd 读取子进程输出用的正是控制台代码页，所以
  "让调用方改成 UTF-8" 解决不了。生成的配置载荷则**始终写原始 UTF-8 字节** —— 它要落盘或交给内核。
  终端编码对不上时（例如 PowerShell 7 固定按 UTF-8 解码子进程输出）可用环境变量显式指定：
  `SUBCONV_CONSOLE_ENCODING=utf-8`（或 `936`、`auto`，默认 `auto`）。
- **每个里程碑都用真实内核校验**。

## 已知差异与注意事项

- **Xray v25+ 移除了 `allowInsecure`**：加载时报
  `The feature "allowInsecure" has been removed and migrated to "pinnedPeerCertSha256"(pcs) and "verifyPeerCertByName"(vcn)`。
  两条替代路径语义不同：`pinnedPeerCertSha256` 命中叶子证书即放行（等价于 `skip-cert-verify`），
  而 `verifyPeerCertByName` 仍要求「链可信 **且** 名字匹配」。因此 subconv 提供 `--probe-cert`
  探测指纹写入 `pinnedPeerCertSha256` / `pcs=`，探不到时退回 vcn 并告警 —— 详见
  「[证书校验](#证书校验为什么同一份订阅在-clash-里能用在-xray-里全是--1)」。
- **传输层能力差异**：`network: xhttp` 在 mihomo 里**只支持 vless 出站**（vmess / trojan 会被
  跳过并告警）；**sing-box 1.14 根本没有 xhttp**（`sing-box check` 报 `unknown transport type`），
  这些节点在 `singbox` 目标会被跳过而不是降级成 tcp。反之原版 Clash / 老版本 mihomo 也不认 xhttp。
- **VLESS Encryption（`encryption=mlkem768x25519plus.…`）是 Xray / mihomo 的扩展**：mihomo 与
  Xray 都认，**sing-box 1.14 不认**（这些节点会被跳过而不是产出加载不了的配置）。**丢了它不会报错、
  只会静默超时**：服务端解不开 VLESS 头部就既不回包也不关连接，客户端一路等到 dial timeout ——
  详见「[VLESS Encryption](#vless-encryption为什么节点连不上却没有任何报错)」。
- **`--probe-cert` 会主动连接节点**：默认关闭（转换器平时只做本地转换），且需要在构建时找到
  OpenSSL；探不到的节点只是告警，不会让转换失败。
- **原版 Clash 语法**（`--clash-legacy`）不支持 vless / hysteria2 / tuic / ssr / snell，
  这些节点会被跳过并写入配置头部注释。
- **HTTPS 抓取报 `error adding trust anchors from file`**：MSYS2 的 `ucrt64` 偶尔会留下
  **0 字节**的 `etc/ssl/certs/ca-bundle.crt`，而 libcurl 的内置默认路径正指向它。subconv 会
  自动退回到 `<MSYS2>/usr/ssl/certs/ca-bundle.crt` 等可用位置，`subconv version` 会打印实际
  生效的路径；也可用环境变量 `CURL_CA_BUNDLE` 指定，或用 `-k` 跳过校验。
- `GEOIP` 规则需要 `geoip.metadb`；mihomo 首次加载会自动下载，离线环境请手动放置。
- **`v2rayn` 是单向输出**：项目里没有 `v2rayn://` 的**输入**解析器，所以它无法参与往返测试，
  只能靠 `tools/verify-v2rayn.ps1` 重放两端逻辑验证。
- **WG / JSON 配置输入未实现**：`wireguard://`、`wg://` 会被识别但跳过；把 Xray / sing-box 的
  JSON 配置当输入会得到明确报错。**Clash 方言的 JSON**（面板 `?app=clash` 返回的那种）已经支持 ——
  它按 Clash YAML 解析，因为 JSON 就是 YAML 的子集。

## 构建环境

本地开发机（Windows）：

| 项 | 版本 |
|---|---|
| 编译器 | MSYS2 UCRT64 `g++` **16.2.0**（C++23，`-Wall -Wextra`） |
| 构建 | CMake **4.4.3**（要求 ≥ 3.20）+ Ninja **1.13.2** |
| libcurl（可选） | `mingw-w64-ucrt-x86_64-curl` **8.22.0** |
| OpenSSL（可选） | `mingw-w64-ucrt-x86_64-openssl` **3.6.4** —— `--probe-cert` 用它取证书指纹 |
| yaml-cpp（可选） | `mingw-w64-ucrt-x86_64-yaml-cpp` **0.9.0** |
| JSON | vendor 的 `third_party/nlohmann/json.hpp`（MIT） |
| 校验内核 | mihomo v1.19.30、Xray v26.9.9、sing-box v1.14.0（`tools\bin\`，不入库） |

Release 里的 Windows 包不是在这台机器上编的，而是 CI 用 **Linux + llvm-mingw 交叉编译**出来的：

| 项 | 版本 |
|---|---|
| 工具链 | llvm-mingw **20260908**（clang **23.1.1** + lld，UCRT + libc++），一份含 x86_64 / aarch64 |
| 交叉依赖 | OpenSSL **3.5.1**（`no-asm no-module`）+ libcurl **8.15.0**（静态、砍掉 zlib/brotli/zstd/nghttp2/libssh2…），由 `tools/build-windows-deps.sh` 现编并缓存 |
| yaml-cpp | 0.8.0 源码静态编入（`-DSUBCONV_VENDOR_YAMLCPP=ON`） |
| 跑测试 | CI 不跑 Windows 产物；靠架构断言（`coff-x86-64` / `coff-arm64`）确认产物是对的平台。要跑测试就在 Windows 上执行 `subconv_tests.exe`（937 项断言）或用 MSYS2 本地构建 |
| exe 依赖 | 只有系统 DLL：`KERNEL32` / `USER32` / `ADVAPI32` / `WS2_32` / `CRYPT32` / `bcrypt` + UCRT 的 `api-ms-win-crt-*`，第三方 DLL 一个都不带 |

代码本身是平台无关的：平台相关的地方都收在几处 `#ifdef _WIN32` 里 ——

| 位置 | Windows | 其它平台 |
|---|---|---|
| `src/server/http.cpp` | WinSock（`winsock2.h` + `ws2_32`） | BSD socket（`sys/socket.h`） |
| `src/fetch/certprobe.cpp` | `WSAStartup` / `ioctlsocket` / `select` | 非阻塞 `connect` + `fcntl` + `select` |
| `src/core/console.cpp` | 按控制台代码页 `WideCharToMultiByte` | 原样输出 UTF-8 |
| `src/core/fsutil.cpp` | `_mkdir` / `_access` / `_stat` | `mkdir` / `access` / `stat` |
| `src/server/http.cpp` `open_browser` | `start` | `xdg-open`（Linux / Termux） |
| `src/cli/main.cpp` | `gmtime_s` | `gmtime_r` |

一次 Linux（WSL Ubuntu 24.04 / GCC 13）验证的实测结果：`./build.sh --clean --test --vendor-yaml
--static-runtime` 编译通过，858 项断言全过；产物 `ldd` 只剩 `libcurl.so.4` / `libssl.so.3` / `libc.so.6`，
没有 `libyaml-cpp` 也没有 `libstdc++`。

## 许可证

仓库内**没有 `LICENSE` 文件**，即默认保留所有权利。若要开源发布，请自行补一份许可证
（MIT / Apache-2.0 等）—— 注意 vendor 的 `third_party/nlohmann/json.hpp` 本身是 MIT 许可。
