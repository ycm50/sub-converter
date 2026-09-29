# Chain proxy (前置 / 中转) — facts for a subconverter-style project

Research notes for the chain-proxy feature (`--chain` / `?chain=` / Web UI 的「链式代理」).
All claims sourced from primary material (official docs / raw source). Unverified items in §6.

---

## 1. The field in each kernel

| Kernel | Where the chain lives | Value | Source |
|---|---|---|---|
| Xray-core | `outbounds[].streamSettings.sockopt.dialerProxy` | **出站 tag** | <https://xtls.github.io/config/transports/sockopt.html> |
| mihomo (Clash.Meta) | proxy-level `dialer-proxy` | 代理名 **或代理组名** | <https://wiki.metacubex.one/config/proxies/dialer-proxy/> |
| sing-box | outbound / endpoint `detour`（Dial Fields） | **出站 tag** | <https://sing-box.sagernet.org/configuration/shared/dial/> |

Quotes (translated from the Chinese docs):

- Xray `dialerProxy`: “一个出站代理的标识。当值不为空时，将使用指定的 outbound 发出连接。通常用于配置链式代理。”
- sing-box `detour`: “The tag of the upstream outbound. *If enabled, all other fields will be ignored.*”
- mihomo `dialer-proxy`: 值可以是「策略组 / 出站代理」，官方示例里直接写代理组名（如 `dialer-proxy: select1`）。

**Semantics used by this project**: `本地 → chain[0] → chain[1] → … → 每个节点`. Each outbound that
participates in the chain gets the *previous* hop as its dialer; the outermost hop dials directly.

### 后置（`--chain-rear`）—— 同一个字段，方向相反

字段的含义是「**本出站经谁出去**」，所以链路的方向和「谁拨号到目标」是反的。`--chain` 让**节点**
自己拨号到目标，于是节点的 `dialerProxy` 指向链路末端；`--chain-rear` 要让**后置链路的末端**拨号到
目标，于是流量落点必须跟着挪到末端：

```text
本地 → chain[0] → … → chain[n] → 节点 → rear[0] → … → rear[m] → 目标
                                   ↑                     ↑
                        节点的 dialerProxy        末端的 dialerProxy 指向它前面一跳
```

实测（Xray 26.6.1，本仓库 tools 之外的手工端到端）：把 `trojan://…@<cf-pages>.pages.dev` 当入口、
`vless://…@<vps>:30000` 当后置，生成的配置 `xray run -test` 报 `Configuration OK`；实跑出口 IP 是
**VPS 自己的 IP**（不是 Cloudflare），`www.speedtest.net` 返回 200。而同一个入口在**前置**方向
（`--chain <vps>`）下出口是 Cloudflare 边缘，`www.speedtest.net` 在 0.65s 内被 Worker 直接关掉
（trojan 层 TLS 1.3 握手成功、请求发出后 `websocket: close 1005`）。入口能不能连通、出口能不能访问
目标站，是两件独立的事 —— 这就是后置存在的意义。

两条实现上的推论：

1. 后置链路必须**每个节点克隆一份**：`rear[0]` 得经各自的节点出去，复用同一条出站做不到。
   于是 N 个节点 × m 跳 = N×m 条出站。
2. 落点出站的名字必须**明确写出链路**（`节点 → 后置`），不能顶替节点原名。出站名与它里面的配置
   对不上，是「看起来对、其实链错了」的典型；本项目一贯拒绝这种表述（同 §2.6 的精神）。
   代价是节点名会变长，换来的是生成物自解释、以及分流规则引用出站名时不会指错对象。

## 2. Known constraints / gotchas

1. **TCP tunnels only.** All three implementations build a TCP connection to the proxy server through
   the chain. A hop whose own transport is UDP-based cannot be reached that way: hysteria /
   hysteria2 / tuic / wireguard, or any node on `quic` / `kcp` transport. mihomo's own doc warns
   about exactly this (“请勿选择任何 udp 类协议如 hy2/tuic/wg”).
   The predicate is `protocol_is_udp_transport()` (`include/subconv/types.hpp`, which replaced the
   unused-and-wrong `protocol_is_dialer`); the chain-position check is `chain_tunnel_blocker()`
   (`src/emit/chain.cpp`).

   **Enforcement** — previously this was a warning only, which produced configs that load and then
   time out forever. Now:

   * a **hop** at an illegal position makes the conversion **fail** (it is user-specified
     infrastructure; dropping it would silently dismantle the chain);
   * a **subscription node** that a front chain would tunnel but which is UDP-based is **skipped with
     an explicit reason** — it is only a candidate, so dropping it does not route any traffic
     directly and does not leak the real IP.

   The resulting rule: a UDP-based protocol may only occupy the **outermost** position
   (`chain[0]`, the one hop that dials directly). In a rear chain every hop is tunneled by
   definition, so all of them must be TCP-based. A rear-only chain does **not** tunnel the
   subscription nodes, so a subscription full of hysteria2/tuic nodes still converts fine there.

2. **Xray `happyEyeballs` conflicts with `dialerProxy`** (documented on the sockopt page). subconv never
   emits `happyEyeballs`, so this cannot bite — but it means a hand-written config that does enable it
   cannot be combined with a chain.
3. **WireGuard** is UDP-based (its outbound dials the endpoint over UDP), so it follows rule (1):
   as a *hop* it is only legal at `chain[0]`; anywhere else the conversion fails or the candidate
   node is skipped. WireGuard still works as the outermost hop — it dials directly, and the next
   hop dials TCP *through* the tunnel.
4. **Referencing a proxy group** works in mihomo but not in subconv: our chain items are nodes
   (share links or subscription node names), never groups.
5. **Ordering of outbounds** does not matter to any of the three at parse time (all resolve tags at
   connection time). subconv emits hops first for readability.
6. **A reference that cannot be resolved must be reported.** Dropping a chain silently produces a
   *direct* config — the traffic exits with the user's real IP. That is the one failure mode this
   feature must never hide: subconv fails the conversion when a user-specified hop cannot be emitted,
   and warns loudly when an inherited `dialerProxy` reference is missing from the output.
7. **Cycles are NOT rejected by Xray.** Measured on Xray 26.9.9: a config whose two outbounds point
   `dialerProxy` at each other (`A → B → A`) passes `xray run -test` with `Configuration OK` — the
   failure only shows up when a connection is attempted. A converter that emits such a config hands
   the user something that loads and never connects, so subconv detects cycles itself (three-colour
   DFS over the resolved reference graph) and fails with
   `输入配置的链式代理成环：A -> B -> A`. The self-reference case (`A → A`) is instead downgraded to
   direct with a warning.

## 3. Input side (fidelity)

| Input | Field | Model |
|---|---|---|
| Xray JSON | `streamSettings.sockopt.dialerProxy` | `ProxyNode::dialer_proxy` (+ `source_name` = outbound `tag`) |
| Clash YAML | `dialer-proxy`（字符串形态） | `ProxyNode::dialer_proxy` (+ `source_name` = proxy `name`) |

`source_name` is what makes the round-trip possible: the reference is written in the *source*
namespace (outbound tag / proxy name), while subconv renames nodes (emoji, de-duplication suffix,
`remarks`-derived names). At emit time the reference is looked up in an index built from
`source_name` + final name (`build_dialer_index`).

**Ambiguous references are refused, not guessed.** If two *different* outbounds carry the same
`source_name` (a `tag`/proxy-name collision in the input — which real panels do produce), then a
reference to that name cannot be resolved deterministically. `build_dialer_index` marks such a key
with an empty target and `effective_dialer` warns
(`… 在输入里有歧义（有两个不同的出站用了同一个标识），已按直连处理`) instead of picking whichever
node happened to come first in subscription order. Two nodes that are *the same* node (`source_name`
equals its own final name) are not ambiguity and resolve normally.

Clash YAML list-form `dialer-proxy: [a, b]` is **not** parsed — no authoritative statement about the
direction of the list was found, and guessing would produce a chain that looks wired but is not.
It is ignored with a warning instead.

## 4. What subconv emits

- **Xray**: hop outbounds appended (only external share-link hops; a hop that names an existing
  subscription node reuses that node's outbound), nodes get `sockopt.dialerProxy`.
  Hops are excluded from `observatory.subjectSelector` and from the `auto` balancer selector.
- **mihomo**: hop proxies appended, nodes get `dialer-proxy:`. Hops are excluded from
  `proxy-groups` (their names must merely exist in `proxies:`).
- **sing-box**: hop outbounds / endpoints appended (endpoints still come first in the document),
  nodes get `detour`. Hops are excluded from `selector` / `urltest`.
- **Unsupported hop protocols carry an actionable hint.** When the chosen target cannot express a
  hop (`socks5/http/ss/vmess/vless/trojan/wireguard` in Xray; `ss/vmess/vless/trojan/hysteria/
  hysteria2/tuic/socks5/http/wireguard` in sing-box, minus xhttp and VLESS-Encryption; everything
  in mihomo), the error does not stop at “不支持该协议” — `target_capability_hint()` names the
  targets that *can* express it (e.g. “这个跳点请改用 -t clash 或 -t singbox”). That is what makes
  “chains support every protocol” usable in practice: the user is told where to send the config
  instead of being left to guess.
- **links / base64 / v2rayn**: share-link formats have no field for this; `--chain` is ignored with
  an explicit warning.

  Deliberate asymmetry with the "must fail" rule above: when a *hop* cannot be emitted the conversion
  fails, because the user's infrastructure would silently vanish. When the *target itself* cannot
  express a chain, no node data is lost and the format is inherently chain-less — that is a
  target/option mismatch, which this project reports as a warning elsewhere too (`--dns` /
  `--rulesets` are Clash-only and behave the same way). It is emitted as the first warning for that
  target and names the fix (`需要链路请用 -t clash / -t xray / -t singbox`).

### 传输路径归一化

`ws` 的 `path` 在三个目标里统一过 `normalize_transport_path()`（空 → `/`，非空缺前导 `/` 则补上），
分享链接回写也走同一个函数。内核是按 HTTP 请求行拼这个路径的：mihomo 自己会补
（`transport/vmess/websocket.go`：`if !strings.HasPrefix(uri.Path, "/")`），Xray / sing-box 不保证 ——
所以不能把「能不能连上」交给对端实现的宽容度。

## 5. Validation performed

`tools/validate.ps1 -Chain <hop>[|<hop>…]` runs `mihomo -t`, `xray run -test`, `sing-box check` over
the generated files (schema level); all three accept a two-hop chain.

A separate end-to-end check (script kept outside the repo, run manually with the same kernels) stood up
a three-tier topology

```
curl → [kernel inbound] → FRONT (Python SOCKS5) → EXIT-NODE (Python SOCKS5) → HTTP target
```

and asserted, from the two proxies' logs, that the front was asked to connect to the *exit node* and
the exit node was asked to connect to the *target* — plus a counter-test proving that without
`--chain` the front sees no traffic at all. Result: **all three kernels pass** (sing-box 1.14.0,
Xray 26.9.9, mihomo 1.19.30 on Windows).

Two things that made that harness work, worth remembering:

- The destination must be a **public** address: both Xray and mihomo route loopback/private
  destinations DIRECT, and mihomo's `geoip.metadb` classifies TEST-NET (`198.51.100.0/24`) as
  `private` too — with those, the chain is never touched. The exit SOCKS5 therefore rewrites the
  requested address to a local target and logs the *original* one.
- The health checks (`url-test` / `observatory` / `urltest`) must succeed, otherwise the group never
  selects the node; the target server answers `204` for `*generate_204*` and `200 OK-CHAIN` otherwise.

## 6. Unverified / open

- mihomo's list-form `dialer-proxy` direction and whether it is a chain at all (§3) — ⚠️ UNVERIFIED.
- Whether Xray's `dialerProxy` supports UDP (`XUDP`) through a proxy dialer. Docs only say
  “使用指定的 outbound 发出连接”; the TCP-only assumption is corroborated by (1) and (3) but not
  stated outright — ⚠️ UNVERIFIED.
- sing-box `detour` on a **wireguard endpoint**: the field is documented in Dial Fields and `sing-box
  check` accepts it, but runtime behaviour with a UDP endpoint behind a TCP chain was not tested.
- Cycle detection (§2.7) covers the resolved `dialer_proxy` graph. It is **not** applied when
  `--chain` is given (there, hops are built from user input in order, so a cycle is impossible by
  construction — a repeated node is rejected earlier).
- `protocol_is_dialer` was removed: it had no call sites and returned `true` for hysteria /
  hysteria2 / tuic / wireguard, i.e. exactly the protocols that *cannot* be carried by a tunnel.
  `protocol_is_udp_transport` states the real question and is used by
  `chain_tunnel_blocker`.
