# WireGuard facts for a subconverter-style C++ project (WG support)

Research notes. All claims sourced from primary material (official docs / raw source). Unverified items collected in §5.

---

## 1. Share-link / URI conventions

### 1.1 Is there a de-facto `wireguard://`? — YES, but only in the v2rayN/v2rayNG ecosystem

- **v2rayN**: `Global.ProtocolShares` maps `EConfigType.WireGuard → "wireguard://"` (also `vmess://`, `vless://`, `trojan://`, `hysteria2://`, `tuic://`, `anytls://`, `naive://`, `ss://`, `socks://`). There is **no `wg://`**.
  Source: <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Global.cs>
- **v2rayNG**: same scheme, implemented in `WireguardFmt.parse/toUri` (delegating to `FmtBase.toUri`).
  Source: <https://raw.githubusercontent.com/2dust/v2rayNG/master/V2rayNG/app/src/main/java/com/v2ray/ang/fmt/WireguardFmt.kt>
- **mihomo/Clash.Meta has NO share-link parser at all** for WireGuard (see §1.3). No spec defines a URI scheme — this is a de-facto convention between 2dust's clients.

**Exact syntax** (derived from `BaseFmt.ToUri` + `WireguardFmt.ToUri`):

```
wireguard://<URL-encoded-client-private-key>@<endpoint-host>:<endpoint-port>?publickey=<b64>&presharedkey=<b64>&reserved=<b1,b2,b3>&address=<cidr,cidr>&mtu=<int>&dns=<ip,ip>&fm=<json>#<URL-encoded-remarks>
```

- `userinfo` = **client private key** (base64; URL-encoded by v2rayN — mandatory because base64 contains `+`, `/`, `=`)
- `host:port` = WireGuard **peer endpoint** (IPv6 bracketed via `GetIpv6`)
- query names are **lowercase, no separators**: `publickey`, `presharedkey`, `reserved`, `address`, `mtu`, `dns`, and `fm` (FinalMask JSON, an Xray-core extension)
- `fragment` = remark/name
- v2rayN's field order (fixed by dictionary insertion order in `WireguardFmt.ToUri`): `publickey, presharedkey, reserved, address, mtu, dns, fm`
- `reserved` in the link is a **comma-separated decimal string**. v2rayN writes the raw stored string; v2rayNG `toUri` normalises with `removeWhiteSpace()` → `"209,98,59"`; v2rayNG `parse` defaults it to `"0,0,0"`.

Sources: <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Handler/Fmt/BaseFmt.cs> , <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Handler/Fmt/WireguardFmt.cs>

### 1.2 v2rayN / v2rayNG WireGuard JSON field names (EXACT)

There is **no WireGuard-specific JSON blob like the `vmess://` one for the public share link** — the public share link is the URI above. JSON appears in two places:

**(a) v2rayN `ProtocolExtraItem`** (per-node extra fields; private key is NOT here — it lives in `ProfileItem.Password`, endpoint in `ProfileItem.Address`/`Port`):

```csharp
// wireguard
public string? WgPublicKey { get; init; }
public string? WgPresharedKey { get; init; }
public string? WgInterfaceAddress { get; init; }
public string? WgReserved { get; init; }
public int?    WgMtu { get; init; }
public string? WgDns { get; init; }
```

Source: <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Models/Entities/ProtocolExtraItem.cs>

**(b) v2rayNG `V2rayNShareItem`** — this is the `v2rayn://<base64 JSON>` internal share format:

```kotlin
data class V2rayNShareItem(
  val IndexId: String?, val ConfigType: Int?, val ConfigVersion: Int?, val Subid: String?,
  val IsSub: Boolean?, val PreSocksPort: Int?, val Remarks: String?,
  val Address: String?, val Port: Int?, val Password: String?,   // Password == WireGuard private key
  val Username: String?, val Network: String?, val StreamSecurity: String?,
  val AllowInsecure: String?, val Sni: String?, val Alpn: String?, val Fingerprint: String?,
  val PublicKey: String?, val ShortId: String?, val SpiderX: String?, val Mldsa65Verify: String?,
  val CertSha: String?, val EchConfigList: String?, val VerifyPeerCertByName: String?,
  val Finalmask: String?,
  val ProtoExtraObj: V2rayNProtocolExtraShareItem?,
  val TransportExtraObj: V2rayNTransportExtraShareItem?,
)
data class V2rayNProtocolExtraShareItem(
  val AlterId: Int?, val VmessSecurity: String?, val Flow: String?, val VlessEncryption: String?,
  val SsMethod: String?,
  val WgPublicKey: String?, val WgPresharedKey: String?, val WgInterfaceAddress: String?,
  val WgReserved: String?, val WgMtu: Int?,            // NOTE: no WgDns here (v2rayN has it; v2rayNG omits it)
  ...
)
```

`ConfigType` enum in the share item: `1=VMess, 2=CUSTOM, 3=Shadowsocks, 4=SOCKS, 5=VLESS, 6=Trojan, 7=Hysteria2, 9=WireGuard, 10=HTTP, 101=PolicyGroup, 102=ProxyChain` (8/TUIC commented out). `toProfileItem()` maps `secretKey = Password` when `configType==9`, plus `preSharedKey/localAddress/reserved/mtu` from `ProtoExtraObj`.

Sources: <https://raw.githubusercontent.com/2dust/v2rayNG/master/V2rayNG/app/src/main/java/com/v2ray/ang/dto/V2rayNShareItem.kt> , <https://raw.githubusercontent.com/2dust/v2rayNG/master/V2rayNG/app/src/main/java/com/v2ray/ang/fmt/V2rayNFmt.kt>

`v2rayn://` encoding on the v2rayN side: `Global.InnerUriProtocol = "v2rayn://"`; v2rayNG decodes with `Utils.decode(str.substringAfterLast('/'))` (base64) then Gson → `V2rayNShareItem`.

⚠️ **UNVERIFIED**: whether v2rayN emits one JSON object per line or a JSON array (v2rayNG's `parse(list: List<String>)` handles one item per line and de-dups by `IndexId`).

**Straight answer**: v2rayN/v2rayNG do **not** use a `vmess://`-style base64 JSON for WireGuard share links; they use `wireguard://…?publickey=&reserved=…`. Base64 JSON appears only in the `v2rayn://` *internal* transfer format.

### 1.3 Does Clash/mihomo support a `wg://` share link? — **NO**

- mihomo's proxy factory is mapping-only: `adapter.ParseProxy` switches on `mapping["type"].(string)` (`"wireguard"`, `"ss"`, `"vmess"`, …) and returns `unsupport proxy type` otherwise — no URI-scheme branch.
  Source: <https://raw.githubusercontent.com/MetaCubeX/mihomo/Alpha/adapter/parser.go>
- Provider content may be `yaml`, `uri` or `base64`, but the documented URI examples are v2ray/xray-style (`ss://…`, `vmess://…`).
  Source: <https://wiki.metacubex.one/en/config/proxy-providers/content/>
- WireGuard is documented **only** as a YAML proxy (`type: wireguard`).
  Source: <https://wiki.metacubex.one/en/config/proxies/wg/>
- ⚠️ **UNVERIFIED**: whether some third-party Clash GUI (Clash Verge/FlClash/etc.) accepts `wireguard://` and converts it before handing YAML to the core.

### 1.4 `reserved` — meaning and exact formats

**Meaning** (primary): a WireGuard handshake message = 1-byte type + **3 reserved (nul) bytes** + fields. wireguard-go comments this explicitly:

```go
/* Type is an 8-bit field, followed by 3 nul bytes,
 * by marshalling the messages in little-endian byteorder
 * we can treat these as a 32-bit unsigned int (for now)
 */
type MessageInitiation struct { Type uint32; Sender uint32; ... }
```

Source: <https://raw.githubusercontent.com/WireGuard/wireguard-go/master/device/noise-protocol.go>

Cloudflare WARP repurposes those 3 normally-zero bytes to carry the account's `client_id`:

- "A tc-bpf action to rewrite `wg.reserved_zero[3]` to `client_id` required by warp" — <https://raw.githubusercontent.com/xdqi/warp-ebpf/master/README.md>
- Related gist (rewrite 3 bytes `wg.reserved` for WARP): <https://gist.github.com/iBug/3107fd4d5af6a4ea7bcea4a8090dcc7e>

**Formats seen in the wild — all reduce to 3 bytes:**

| Encoding | Example | Where |
|---|---|---|
| JSON array of 3 ints | `"reserved": [0, 0, 0]`, `[209,98,59]` | Xray-core outbound, sing-box (outbound + endpoint peer) |
| YAML array of 3 ints | `reserved: [209,98,59]` | mihomo (Go type `[]uint8`; mihomo **errors unless exactly 3 bytes**) |
| YAML/JSON string (base64 of the 3 bytes) | `reserved: "U4An"` | mihomo docs: "String format is also valid, such as `U4An`" |
| Comma-separated decimal string | `reserved=209,98,59`, `Reserved = 1, 2, 3` | v2rayN/v2rayNG URI query; non-standard `[Peer] Reserved =` in .conf |

Sources: <https://xtls.github.io/en/config/outbounds/wireguard.html> , <https://wiki.metacubex.one/en/config/proxies/wg/> , <https://raw.githubusercontent.com/MetaCubeX/mihomo/Alpha/adapter/outbound/wireguard.go> , <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Handler/Fmt/WireguardFmt.cs>

⚠️ **UNVERIFIED**: how mihomo decodes the *string* form (`"U4An"` → 3 bytes).

### 1.5 Standard WireGuard `.conf` keys (exact spellings)

```ini
[Interface]
PrivateKey = <base64>
Address    = 10.0.0.2/32, fd00::2/128
ListenPort = 51820
DNS        = 1.1.1.1, 2606:4700:4700::1111
MTU        = 1420

[Peer]
PublicKey           = <base64>
PresharedKey        = <base64>
AllowedIPs          = 0.0.0.0/0, ::/0
Endpoint            = [2001:db8::1]:51820
PersistentKeepalive = 25
```

- Key names confirmed by mihomo's "Translating from a standard WireGuard configuration file" section (`Address, ListenPort, PrivateKey, DNS, MTU` / `AllowedIPs, Endpoint, PublicKey`) — <https://wiki.metacubex.one/en/config/proxies/wg/>
- Confirmed independently by two production parsers: v2rayN `WireguardFmt.ResolveConfig` reads `PrivateKey, Address, MTU, DNS` (Interface) and `Endpoint, PublicKey, PresharedKey, Reserved` (Peer) — canonical spelling is **`PresharedKey`**, not `PreSharedKey`; v2rayNG `parseWireguardConfFile` lowercases keys and reads `privatekey, address, mtu, dns, publickey, presharedkey, endpoint, reserved, finalmask/fm`.
- **Non-standard extensions** accepted by 2dust clients: `Reserved = 1, 2, 3` inside `[Peer]` (v2rayN + v2rayNG), and `Finalmask`/`fm`.
- Parsing behaviour worth copying: v2rayN strips inline comments after `#`/`;`, handles `[IPv6]:port` endpoints, defaults peer port to **2408**, and turns **each `[Peer]` into a separate ProfileItem** (`WireGuard Peer 1`, `Peer 2`, …). v2rayNG supports **only one peer** (later `[Peer]` sections overwrite) and splits endpoint with `split(":", limit = 2)` — i.e. it **mis-parses bracketed IPv6 endpoints**. Recommend copying v2rayN's behaviour.

⚠️ I could **not** fetch `wireguard.com/protocol/` or `/quickstart/` (repeated network failures) — the key list above is corroborated by the mihomo doc + two client parsers instead of the official man page.

---

## 2. Outbound schema per kernel (verbatim)

### 2.1 mihomo / Clash.Meta — `type: wireguard`

Doc: <https://wiki.metacubex.one/en/config/proxies/wg/>

```yaml
proxies:
  - name: "wg"
    type: wireguard
    ip: 172.16.0.2              # IPv4 local addr (no CIDR ok -> /32 added)
    ipv6: fd01:5ca1:ab1e:80fa:ab85:6eea:213f:f4a5   # optional -> /128
    private-key: <base64>       # REQUIRED
    server: 162.159.192.1       # single-peer form
    port: 2480
    public-key: <base64>
    pre-shared-key: <base64>    # optional
    reserved: [209, 98, 59]     # optional; "U4An" string also documented
    allowed-ips: ['0.0.0.0/0']
    persistent-keepalive: 25
    mtu: 1408
    udp: true
    workers: 4
    ip-stack:                   # optional
      mode: auto                # auto | gvisor | mips
      congestion-controller: cubic   # cubic | reno | bbr | bbr3
    dialer-proxy: "ss1"         # common proxy field
    remote-dns-resolve: true
    dns: [1.1.1.1, 8.8.8.8]
    refresh-server-ip-interval: 0
    amnezia-wg-option: {...}    # see §3.3
```

**Multi-peer form** — top-level `server`/`port`/`public-key`/`pre-shared-key`/`reserved` are **ignored**; `private-key` stays top-level; each peer needs a *distinct* `allowed-ips`:

```yaml
proxies:
  - name: "wg"
    type: wireguard
    ip: 172.16.0.2
    ipv6: fd01:...
    private-key: <base64>
    peers:
      - server: 162.159.192.1
        port: 2480
        public-key: <base64>
        allowed-ips: ['0.0.0.0/0']
        pre-shared-key: <base64>
        reserved: [209,98,59]
    udp: true
    mtu: 1408
```

Go structs (authoritative field names/types): <https://raw.githubusercontent.com/MetaCubeX/mihomo/Alpha/adapter/outbound/wireguard.go>

```go
type WireGuardOption struct {
    BasicOption                                // incl. dialer-proxy, interface, routing-mark, udp
    Name string `proxy:"name"`; Ip string `proxy:"ip,omitempty"`; Ipv6 string `proxy:"ipv6,omitempty"`
    PrivateKey string `proxy:"private-key"`; Workers int `proxy:"workers,omitempty"`
    MTU int `proxy:"mtu,omitempty"`; UDP bool `proxy:"udp,omitempty"`
    PersistentKeepalive int `proxy:"persistent-keepalive,omitempty"`
    IPStack IPStackOption `proxy:"ip-stack,omitempty"`
    AmneziaWGOption *AmneziaWGOption `proxy:"amnezia-wg-option,omitempty"`
    Peers []WireGuardPeerOption `proxy:"peers,omitempty"`
    RemoteDnsResolve bool `proxy:"remote-dns-resolve,omitempty"`; Dns []string `proxy:"dns,omitempty"`
    RefreshServerIPInterval int `proxy:"refresh-server-ip-interval,omitempty"`
}
type WireGuardPeerOption struct {
    Server string `proxy:"server,omitempty"`; Port int `proxy:"port,omitempty"`
    PublicKey string `proxy:"public-key,omitempty"`; PreSharedKey string `proxy:"pre-shared-key,omitempty"`
    Reserved []uint8 `proxy:"reserved,omitempty"`; AllowedIPs []string `proxy:"allowed-ips,omitempty"`
}
```

- `IsL3Protocol() == true` for WireGuard; MTU defaults to **1408** internally.
- **mihomo has NO WireGuard inbound/listener.** Listener types are anytls, http, hysteria2, jls, kcptun, mekya, mieru, mixed, mkcp, mux, reality, redir, restls, shadowquic, shadowsocks, shadowtls, snell, socks, sudoku, tlsmirror, tproxy, trojan, trusttunnel, tuic, tun, tunnel, vless, vmess — no `wireguard`.
  Source: tree of `MetaCubeX/mihomo:listener` (Alpha) + <https://wiki.metacubex.one/en/config/inbound/listeners/>

### 2.2 sing-box — the version split (IMPORTANT)

**Old flat outbound** (`outbounds` + `"type": "wireguard"`): `server`, `server_port`, `system_interface`, `interface_name`, `local_address` (string array, required), `private_key`, `peers[]` (multi-peer), `peer_public_key`, `pre_shared_key`, `reserved`, `workers`, `mtu` (default 1408), `network`, `gso` (deprecated 1.11), + Dial Fields. When `peers` is used, `server/server_port/peer_public_key/pre_shared_key` are ignored.

- Doc header: **"Deprecated in sing-box 1.11.0 … will be removed in sing-box 1.13.0"**
- Source: <https://raw.githubusercontent.com/SagerNet/sing-box/testing/docs/configuration/outbound/wireguard.md>
- The flat form and a `peers` array coexisted in the deprecated outbound. The multi-peer addition date is **not documented**; only the 1.11 deprecation / 1.13 removal are. ⚠️ UNVERIFIED: which version first added `peers`.

**New endpoint form** (`endpoints[]`, **since sing-box 1.11.0**):

```json
{
  "endpoints": [{
    "type": "wireguard",
    "tag": "wg-ep",
    "system": false,
    "name": "",
    "mtu": 1408,
    "address": [],                 // REQUIRED: local IP prefixes
    "private_key": "",             // REQUIRED
    "listen_port": 10000,
    "peers": [{
      "address": "127.0.0.1",
      "port": 10001,
      "public_key": "",
      "pre_shared_key": "",
      "allowed_ips": [],
      "persistent_keepalive_interval": 0,
      "reserved": [0, 0, 0]
    }],
    "workers": 0,
    "on_demand": false,            // since 1.15.0
    "udp_mapping": {}, "udp_filtering": {}, "udp_nat_max": 0   // since 1.14.0 (UDP NAT fields)
  }]
}
```

Source: <https://raw.githubusercontent.com/SagerNet/sing-box/testing/docs/configuration/endpoint/wireguard.md>
Migration ("Migrate WireGuard outbound to endpoint", §1.11.0): <https://raw.githubusercontent.com/SagerNet/sing-box/testing/docs/migration.md>

**AmneziaWG in sing-box: not documented** — no `jc/jmin/jmax/s1..s4/h1..h4/i1..i5` in either doc. Treat as unsupported (⚠️ absence of docs, not a positive statement). Default MTU 1408.

### 2.3 Xray-core — `protocol: "wireguard"`

⚠️ **Your assumed field names are partly wrong for current docs.** Current field is `secretKey`, not `secret`; `domainStrategy` is not a WireGuard-outbound field (use outbound-level `targetStrategy`); `noKernelTun` exists; `kernelMode` does **not** appear in current docs.

```json
{
  "outbounds": [{
    "protocol": "wireguard",
    "settings": {
      "secretKey": "CLIENT_PRIVATE_KEY",          // string, REQUIRED
      "address": ["10.0.0.1", "fd59:...::1"],     // [string] local addrs (array, no /32 required)
      "peers": [{
        "endpoint": "example.com:2408",           // string, IP or domain, REQUIRED
        "publicKey": "SERVER_PUBLIC_KEY",         // REQUIRED
        "preSharedKey": "PRE_SHARED_KEY",         // optional
        "keepAlive": 0,                           // int seconds, default 0
        "allowedIPs": ["0.0.0.0/0", "::/0"]       // [string], default both
      }],
      "noKernelTun": false,                       // bool
      "mtu": 1420,                                // int, default 1420
      "reserved": [0, 0, 0],                      // [byte] — exactly 3
      "remoteDNS": ["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"]
    }
  }]
}
```

`settings.address` **is an array of strings** — confirmed. `noKernelTun` selects gVisor vs kernel TUN for reassembling inner IP packets; when TUN is used it occupies IPv6 routing table 10230, then 10231, … MTU rationale (1440 IPv4 / 1420 IPv6) is documented.
Source: <https://xtls.github.io/en/config/outbounds/wireguard.html>

**Xray-core also has a WireGuard INBOUND** (`protocol: "wireguard"` with `secretKey`, `peers[{publicKey, preSharedKey, keepAlive, allowedIPs, email, level}]`, `mtu`) — an Xray server can expose a WG endpoint that converts received TCP/UDP into internal Xray requests:
Source: <https://xtls.github.io/en/config/inbounds/wireguard.html>

⚠️ **UNVERIFIED**: the Xray version where `secret` → `secretKey`; `workers`/`version` appear in v2rayN's generic settings model but are not in the current WG outbound doc.

### 2.4 Can v2rayNG/v2rayN import a WireGuard node? — YES, four mechanisms

1. **Raw `.conf` file** (`WireguardFmt.ResolveConfig` / `parseWireguardConfFile`) — see §1.5.
2. **`wireguard://` URI** — see §1.1.
3. **`v2rayn://<base64 JSON>`** internal share item with `ConfigType = 9` — see §1.2.
4. Manual entry via `ServerWireguardActivity.kt` (v2rayNG).

They then build an **Xray** outbound (and, in v2rayN, optionally a **sing-box** outbound). v2rayNG's builder (`CoreOutboundBuilder.toOutboundWireguard`) produces:

```kotlin
settings.secretKey  = profileItem.secretKey
settings.address    = addresses            // List<String>, split on ","
settings.port       = null
settings.peers[0]   = WireGuardBean(publicKey, preSharedKey, endpoint = "<ipv6-wrapped server>:<port>")
settings.mtu        = profileItem.mtu
settings.remoteDNS  = remotes              // List<String>; special value "local" preserved
settings.reserved   = reserved?.split(",")?.map { it.trim().toInt() }
```

and **disables Xray mux for wireguard**. `V2rayConfig.kt` `OutSettingsBean` WireGuard fields: `secretKey: String?`, `peers: List<WireGuardBean>?` with `WireGuardBean(publicKey, preSharedKey, endpoint)`, `reserved: List<Int>?`, `mtu: Int?`, `remoteDNS: List<String>?`, `domainStrategy: String?`.

Sources: <https://raw.githubusercontent.com/2dust/v2rayNG/master/V2rayNG/app/src/main/java/com/v2ray/ang/core/CoreOutboundBuilder.kt> , <https://raw.githubusercontent.com/2dust/v2rayNG/master/V2rayNG/app/src/main/java/com/v2ray/ang/dto/V2rayConfig.kt>

v2rayN's Xray builder (`V2rayOutboundService.FillOutbound`, case `EConfigType.WireGuard`) fills the same shape; its C# model is `Outboundsettings4Ray { secretKey, address (object → list), peers: List<WireguardPeer4Ray{endpoint, publicKey, preSharedKey}>, noKernelTun, mtu, reserved: List<int>, remoteDNS: List<string> }`.

Sources: <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Services/CoreConfig/V2ray/V2rayOutboundService.cs> , <https://raw.githubusercontent.com/2dust/v2rayN/master/v2rayN/ServiceLib/Models/CoreConfigs/V2rayConfig.cs>

`Global.XraySupportConfigType` and `SingboxSupportConfigType` both include `EConfigType.WireGuard`.

---

## 3. Interop matrix

### 3.0 Why WireGuard cannot generally be converted to VMess/VLESS/Trojan (state it like this)

WireGuard is a **layer-3 (IP) tunnel over UDP**. Its data plane carries raw IP packets inside an encrypted, fixed-header UDP flow keyed by Curve25519 public keys; the protocol itself has **no concept of "destination host/port of a proxied request"** and no request/response framing. The peer (server) assigns the client an address inside its tunnel (`Address`/`allowed-ips`) and **routes IP packets** — the server is a router/gateway, not an application proxy.

VMess/VLESS/Trojan (and SS, SOCKS5/HTTP-CONNECT, Hysteria2, TUIC) are **layer-4/7 stream proxies over TCP (or QUIC)**: the client first sends the *target* address/port in a protocol-specific header, and the server opens a connection to that target on the client's behalf.

Consequences for a converter:

- No field-level mapping exists: WG has `private-key/public-key/psk/allowed-ips/endpoint`; a stream proxy has `uuid/password/method/transport/tls`. Even the identity semantics differ (WG key pairs per-peer; VMess UUID per user).
- "WG → VMess" would require **re-emitting a whole different server** that terminates WG and re-exposes a stream proxy. That is *chaining/composition* (`client → stream proxy → (server) → WG → internet`), not protocol translation.
- Therefore a "WG node" can only be *re-serialised* into configs that speak WireGuard (mihomo/sing-box/Xray WG outbound, `.conf`, AmneziaWG), or *consumed as an upstream* by a server that also runs a WG client.

Any tool claiming WG→VMess/VLESS conversion is really just re-pointing at a server that terminates WG for you. Label such claims **architecturally impossible**; the legitimate directions are WG↔WG-config and WG→(config for a kernel's WG client).

### 3.1 Which software can *serve* a WireGuard/AmneziaWG endpoint

| Kernel | WG as client/outbound | WG as server/inbound | AmneziaWG |
|---|---|---|---|
| Xray-core | ✅ `protocol: wireguard` outbound | ✅ `protocol: wireguard` inbound (`secretKey`, `peers[{publicKey,…}]`, `mtu`) | ❌ not documented |
| sing-box | ✅ endpoint since 1.11 (outbound `wireguard` deprecated 1.11, removed 1.13) | ✅ same `endpoints[]` entry | ❌ not documented |
| mihomo/Clash.Meta | ✅ `type: wireguard` proxy | ❌ **no WG listener** | ✅ `amnezia-wg-option` |
| AmneziaWG (kernel module / `amnezia-wg-go` fork) | ✅ native | ✅ native | ✅ native |

Sources: Xray outbound/inbound docs above; sing-box endpoint/outbound/migration docs above; mihomo proxy doc + listener tree; <https://docs.amnezia.org/documentation/amnezia-wg/>

Note: Xray's own docs warn: *"The WireGuard protocol is not designed specifically for bypassing firewalls. If used as the outer layer to cross the firewall, its distinct characteristics may lead to the server being blocked."* (on both the outbound and inbound pages).

### 3.2 Conversion feasibility

| Target | WG→X | X→WG | Notes |
|---|---|---|---|
| Shadowsocks / SSR | ❌ impossible | ❌ impossible | different layers; SS has no IP-tunnel mode |
| VLESS / VMess / Trojan | ❌ impossible | ❌ impossible | see §3.0 |
| Hysteria2 / TUIC | ❌ impossible | ❌ impossible | QUIC-based stream proxies; no L3 |
| SOCKS5 / HTTP | ❌ impossible as protocol | ❌ impossible | but WG can *carry* these (WG client + local SOCKS inbound) |
| IPsec / IKEv2 | ❌ | ❌ | separate key/SA model |
| OpenVPN | ❌ | ❌ | mihomo has only an OpenVPN **outbound** (`type: openvpn`); no WG interchange |
| **AmneziaWG** | ✅ same protocol family (WG is the degenerate config) | ✅ | obfuscation params optional; AWG with all params disabled == WireGuard |
| **WARP** | ✅ (WARP *is* a WG endpoint → emit Xray/sing-box/mihomo WG config) | ✅ | the real-world "convert WARP to proxy" case |
| **Tailscale** | ❌ | ❌ | WireGuard-derived crypto but an entirely different control plane/keys |
| **ZeroTier** | ❌ | ❌ | own protocol/network-ID model; mihomo has a `type: zerotier` outbound only |
| `.conf` / `wireguard://` / YAML / sing-box JSON / Xray JSON / AmneziaWG | ✅ … | ✅ | **these** are the legitimate conversion targets |

mihomo outbound list incl. `tailscale`, `zerotier`, `easytier`, `openvpn`, `masque`, `ssh` (one-way consumption, no conversion): <https://raw.githubusercontent.com/MetaCubeX/mihomo/Alpha/adapter/parser.go>

### 3.3 AmneziaWG (AWG)

- **What it is**: a fork of **WireGuard-Go** that keeps WireGuard's crypto (Noise_IK, Curve25519, ChaCha20-Poly1305) unchanged and adds transport-layer obfuscation to defeat DPI. Versions: v1.0 (S1–S4, H1–H4, Jc/Jmin/Jmax), **v1.5** (adds S3/S4, I1–I5 CPS signature packets, J1–J3, `itime`, protocol mimicry QUIC/DNS/SIP), **v2.0** (H1–H4 become ranges, random bytes in special packets, J1–J3 removed), **v3.1** (header protection via ChaCha20 + `HeaderProtectionKey`, `ContentPaddingAddition`, custom timings, `RandomTrailers`, `DisableCookies`).
- Parameters: `I1-I5` (CPS strings), `S1-S4` (uint16 random prefixes: `len(init)=148+S1`, `len(resp)=92+S2`, `len(cookie)=64+S3`, `len(data)=payload+S4`), `Jc` (junk packet count), `Jmin/Jmax`, `H1-H4` (`range<uint32>`; H1=1/H2=2/H3=3/H4=4 mean *disabled*), `HeaderProtectionKey` (32-byte), `ContentPaddingAddition`, `RekeyAfterTime`, `RekeyTimeout`, `RejectAfterTime`, `KeepaliveTimeout`, `MaxHandshakeAttempts`, `RandomTrailers`, `DisableCookies`. 0/off/`0-0` = disabled → standard WireGuard. H1–H4 ranges must not overlap.
  Source: <https://docs.amnezia.org/documentation/amnezia-wg/>
- **Kernel support**: **mihomo** via `amnezia-wg-option` (full param list incl. v3.1 flags); **sing-box / Xray: not documented** (⚠️ treat as unsupported).
- AWG `.conf` uses the same `[Interface]`/`[Peer]` plus `Jc/Jmin/Jmax/S1/S2/H1…` keys.

### 3.4 Tools that DO convert WireGuard configs (and subconverter's actual support)

**subconverter (tindy2013/subconverter)** — directly relevant:

- `ProxyType::WireGuard` exists; the `Proxy` struct has WG fields: `SelfIP`, `SelfIPv6`, `PublicKey`, `PrivateKey`, `PreSharedKey`, `DnsServers`, `Mtu` (default 0), `AllowedIPs = "0.0.0.0/0, ::/0"`, `KeepAlive`, `TestUrl`, **`ClientId`**.
  Source: <https://raw.githubusercontent.com/tindy2013/subconverter/master/src/parser/config/proxy.h>
- **Input it accepts**: WireGuard **only from Clash-style YAML** (INI sections), handled in `explodeClash`: `type: wireguard` with `section-name`, `test-url`, and a `[WireGuard <section>]` block containing `self-ip`, `self-ip-v6`, `private-key`, `dns-server` (comma list), `mtu`, `keepalive`, `peer` (parsed by `parsePeers`).
  Source: <https://raw.githubusercontent.com/tindy2013/subconverter/master/src/parser/subparser.cpp>
- **Input it does NOT accept**: ❌ there is **no `wireguard://` branch** in `explode()` — that dispatcher handles only `ssr://`, `vmess://`/`vmess1://`, `ss://`, `socks://`, telegram socks/http, `Netch://`, `trojan://`, `hysteria2://`/`hy2://`, `anytls://`. A subscription containing `wireguard://` lines is **silently skipped**.
- **Output it emits**:
  - Clash: `type: wireguard`, `public-key`, `private-key`, `ip`, `ipv6`, `preshared-key`, `dns`, `mtu` (+ top-level `server`/`port`). It does **not** emit `allowed-ips`, `reserved` or `persistent-keepalive` here.
  - sing-box: `type: wireguard`, `tag`, `local_address` (array), `private_key`, `mtu`, and `peers: [{server, server_port, public_key, pre_shared_key, allowed_ips, reserved}]` — where **`reserved` is derived from `ClientId`** (`stringArrayToJsonArray(x.ClientId, ",")`). This is the **old flat outbound form → will break on sing-box ≥ 1.13**.
  - Loon: `wireguard, interface-ip=, interface-ipv6=, private-key=, dns=/dnsv6=, mtu=, keepalive=, peers=[{…}]`
  - Surge: `wireguard, section-name=<n>` + `[WireGuard <n>] peer = (<generatePeer>)`, and `generatePeer(..., client_id_as_reserved=true)` appends `, reserved = [<ClientId>]`.
  Sources: <https://raw.githubusercontent.com/tindy2013/subconverter/master/src/generator/config/subexport.cpp>

⇒ **Actionable**: to match/beat subconverter you should (a) add a `wireguard://` share-link parser (subconverter lacks it), (b) emit the **sing-box endpoint form** as well as the legacy outbound, (c) support both `reserved: [b,b,b]` and `"b1,b2,b3"` string forms, (d) handle multi-`[Peer]`.

**Other generators worth citing**: `wgcf` (`wgcf generate` → `wgcf-profile.conf`, MTU 1280, optional `PersistentKeepalive`) <https://raw.githubusercontent.com/ViRb3/wgcf/master/README.md> ; `warp-wg` (emits `Reserved` as a comment) <https://raw.githubusercontent.com/osamingo/warp-wg/main/README.md> ; `warp-ebpf` (patches `wg.reserved_zero[3]` in-flight) <https://github.com/xdqi/warp-ebpf> ; community WARP→Xray scripts (e.g. remnawave-scripts PR #34: *"Native Xray wireguard outbound hardcoded `reserved: [0,0,0]`. That generic default works on the anycast endpoint but c…"* — <https://github.com/DigneZzZ/remnawave-scripts/pull/34>).

---

## 4. WARP specifics

- **What it is**: Cloudflare's WireGuard service — register a device against an undocumented Cloudflare API, receive an assigned tunnel address + Cloudflare's WG public key + your `client_id`, then run ordinary WireGuard.
- **Config shape**: locally-generated private key; peer public key from registration; `[Interface] Address = 172.16.0.2/32, <ipv6>/128`; MTU **1280** in `wgcf`'s generated profile ("to ensure maximum compatibility … just like the official Android app"); default endpoint host `engage.cloudflareclient.com`, commonly port **2408**.
  - v2rayN hardcodes WARP host presets: `engage.cloudflareclient.com → 162.159.192.1 / 2606:4700:d0::a29f:c001` (`Global.cs` `PredefinedHosts`), and its `.conf` endpoint parser defaults the port to **2408** when absent.
  - mihomo docs use `162.159.192.1:2480`. WARP accepts several UDP ports — ⚠️ **UNVERIFIED** for the frequently-quoted `500/1701/4500/854`.
- **Special endpoint format?** No — plain `host:port` WireGuard. What is special is the **`reserved` field** (Cloudflare-only requirement) and the undocumented registration API.
- **`reserved` for WARP**: the 3 reserved bytes of the WG handshake header carry the account identity = **the first 3 bytes of the base64-decoded WARP `client_id`**. Symptom if wrong/absent: handshake succeeds but no data flows.
  - `warp-wg`: *"If the standard WireGuard client fails to connect (handshake succeeds but no data flows), Cloudflare may be blocking connections without the correct `Reserved` bytes."* — generated profile carries e.g. `# Reserved = 171, 85, 205`.
  - `warp-ebpf`: rewrites `wg.reserved_zero[3]` to `client_id`; its `config.h` holds `static const __u8 warp_private[3] = {11, 45, 14};` to be set to the *content of `ClientID`*.
  - mihomo docs: `reserved` — "Value of the WireGuard protocol reserved field. **Required by some WARP nodes.**"
  - Xray docs: `reserved` — "The three WireGuard reserved bytes. All three default to 0; set them as needed." (no WARP mention in current doc)
  - subconverter decodes Clash `client_id` → `reserved` on Surge/sing-box output; classic WARP `reserved` values appear as `[209,98,59]` in WARP→Xray guides.
- ⚠️ **Forward-looking risk** (wgcf README): "Cloudflare is migrating from WireGuard to MASQUE. WireGuard-based connections may stop working in the future." mihomo already ships a `type: masque` outbound. Don't hardcode WARP assumptions too deeply.
- ⚠️ **UNVERIFIED**: an official Cloudflare document stating the `reserved` = first-3-bytes-of-client_id rule. All sources found are community/implementation projects (wgcf, warp-wg, warp-ebpf, mihomo, subconverter), which nevertheless agree exactly.

---

## 5. Explicit UNVERIFIED list (do not state as fact)

1. `wireguard.com/protocol/` and `/quickstart/` **could not be fetched** (repeated network errors); the §1.4 reserved-byte statement comes from `wireguard-go` source, and the §1.5 key list from mihomo docs + two client parsers rather than wireguard.com.
2. `v2rayn://` payload is object-per-line vs JSON array.
3. Whether any third-party Clash GUI parses `wireguard://`.
4. The sing-box version that **introduced** `peers` in the flat outbound (only 1.11 deprecation / 1.13 removal documented; endpoint `peers` = 1.11.0).
5. The Xray version where `secret` → `secretKey`; whether `kernelMode`/`domainStrategy` ever existed on the WG outbound (current doc uses `noKernelTun` and outbound-level `targetStrategy`).
6. How mihomo decodes the *string* form of `reserved` (`"U4An"`).
7. AmneziaWG support in sing-box/Xray (not documented — inferred unsupported).
8. WARP UDP ports other than 2408; a first-party Cloudflare statement for the reserved rule.
