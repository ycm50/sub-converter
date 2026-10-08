// 各输出目标的内部声明
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "subconv/convert.hpp"
#include "subconv/json.hpp"

namespace subconv {

/// 去重 + 名称唯一化 + 排序（所有目标共用）。
[[nodiscard]] NodeList prepare_nodes(const NodeList& nodes, const EmitOptions& opts);

/// 在 taken 里给 base 找一个不冲突的名字并写进 taken，返回最终名字。
/// Clash 的 `name:`、sing-box 的 `tag:`、Xray 的 outbound `tag` 都要求唯一。
/// base 为空时用 "node" 兜底（节点名退化成 server:port 是调用方 prepare_nodes 的职责）。
[[nodiscard]] std::string unique_name(std::set<std::string>& taken, std::string base);

/// 生成配置首行注释。
[[nodiscard]] std::string config_header(std::string_view target, std::size_t node_count,
                                        std::string_view name = {});

/// 传输路径（ws / xhttp 的 `path`）的归一化：空串 → `/`，非空且不以 `/` 开头时补上。
///
/// 内核是按 HTTP 请求行拼这个路径的，缺少前导斜杠的写法只有部分实现会自己补
/// （mihomo 的 transport/vmess/websocket.go 会，Xray / sing-box 不保证），
/// 所以统一在产出前normalize，别把「能不能连上」交给对端实现的宽容度。
[[nodiscard]] std::string normalize_transport_path(std::string path);

// --- 链式代理（见 src/emit/chain.cpp）---

/// 链路里的一跳。
struct ChainHop {
  ProxyNode node;        ///< 这一跳的节点
  std::string tag;       ///< 产物里的名字（已唯一化）
  std::string dialer;    ///< 上一跳的 tag；空 = 直连
  bool extra = false;    ///< true = 外部分享链接，要作为额外出站写进产物；
                         ///< false = 复用订阅里已有的那个节点（不重复产出）
};

/// 单个节点的「后置链路」展开结果（见 EmitOptions::chain_rear）。
struct RearChain {
  /// 该节点的后置跳点，**由内到外**；`hops.back()` 就是真正的流量落点。
  /// 每一项都 `extra = true`（每个节点一份克隆，不能复用别处的出站）。
  std::vector<ChainHop> hops;

  [[nodiscard]] bool active() const noexcept { return !hops.empty(); }
  /// 流量落点的 tag（== hops.back().tag）。
  [[nodiscard]] const std::string& target() const noexcept;
};

/// 解析后的链路：hops 与 rear 都为空 = 没有链路。
struct ChainPlan {
  std::vector<ChainHop> hops;

  /// 节点名 → 该节点的后置链路展开。空 map = 没有后置链路。
  /// 每个节点一份（后置第 0 跳必须经它自己出去，不能共享）。
  std::map<std::string, RearChain> rear;

  [[nodiscard]] bool active() const noexcept { return !hops.empty() || !rear.empty(); }
  [[nodiscard]] bool has_rear() const noexcept { return !rear.empty(); }
  /// 最后一跳的名字 —— 普通节点的 dialerProxy / dialer-proxy / detour 就是它。
  [[nodiscard]] const std::string& tail() const noexcept;
  /// 节点在链路生效时该用的 dialer：
  ///   * 引用了订阅里节点的跳点本身也是一个可选中节点，它用**自己那一跳**的 dialer
  ///     （否则会变成「A 通过 A 出去」的自环）；
  ///   * 其余节点一律接到链路末端。
  [[nodiscard]] std::string dialer_for(const std::string& node_name) const;
  /// 该节点的后置链路展开；没有后置返回 nullptr。
  [[nodiscard]] const RearChain* rear_for(const std::string& node_name) const;
};

/// 把 EmitOptions.chain / chain_rear 解析成 ChainPlan（prepared 是已经 prepare_nodes 过的节点表）。
/// 解析不出来的项（链接非法 / 名字不存在 / 重复引用）返回 error —— 链路是用户显式
/// 指定的基础设施，静默降级成直连会把流量全放出去，必须让调用方报错。
[[nodiscard]] Result<ChainPlan> resolve_chain(const NodeList& prepared, const EmitOptions& opts,
                                              std::vector<std::string>* warnings);

/// 输入侧 `dialerProxy` / `dialer-proxy` 的引用表：原标识（与原名）→ 本工具最终用的名字。
/// **值可能是空串**：表示这个标识在输入里有歧义（两条不同出站共用了同一个 tag / 代理名），
/// 此时引用方必须拒绝解析并按直连处理 —— 源配置自己就没法确定指谁，不能按订阅顺序赌。
[[nodiscard]] std::map<std::string, std::string> build_dialer_index(const NodeList& prepared);

/// 节点最终使用的链路名；空串 = 直连。
///   * opts.chain 生效时以 ChainPlan 为准；
///   * 否则用节点自带的 dialer_proxy 反查（输入保真），查不到就告警并按直连处理。
[[nodiscard]] std::string effective_dialer(const ProxyNode& node, const ChainPlan& plan,
                                          const std::map<std::string, std::string>& ref_index,
                                          std::vector<std::string>* warnings);

/// 输入自带的 dialer 引用成环时返回环上的节点名（`A -> B -> A`），否则返回空。
///
/// 为什么必须自己查：**内核不一定拒绝**这种配置 —— 实测 Xray 26 对 A→B→A 的 dialerProxy
/// 报 `Configuration OK`，真正连的时候才会死循环/超时。产出这种配置等于给用户一个
/// 「能加载、永远连不上」的产物，所以这里直接让转换失败。
[[nodiscard]] std::vector<std::string> dialer_cycle(const NodeList& prepared,
                                                    const std::map<std::string, std::string>& ref_index);

/// 链路位置合法性：该节点能否作为「经上一条出站的 TCP 隧道到达」的一跳。
///
/// **只对处于非最外层位置的节点调用**（即它自己带 dialer / dialerProxy / detour）：
/// 自身传输是 UDP 的协议（hysteria / hysteria2 / tuic / wireguard，或 quic / kcp 传输）
/// 到不了 —— 内核会照常加载配置，但连接永远建不起来，表现就是「一直超时」。
/// 能链返回空串；不能链返回一句可直接拼进报错/告警的原因。
[[nodiscard]] std::string chain_tunnel_blocker(const ProxyNode& node);

/// 目标内核表达不了该节点时，给出「改用哪个目标」的可操作建议（不含节点名）。
/// 例如 hysteria2 跳点在 xray 目标下会建议 `-t clash 或 -t singbox`。
/// 没有任何目标能表达时，返回一句「换一条更常规的节点做跳点」。
[[nodiscard]] std::string target_capability_hint(const ProxyNode& node, std::string_view target);

// --- 具体目标 ---
Result<std::string> emit_clash(const NodeList& nodes, const EmitOptions& opts,
                               std::vector<std::string>* warnings = nullptr);
Result<std::string> emit_xray(const NodeList& nodes, const EmitOptions& opts,
                              std::vector<std::string>* warnings = nullptr);
Result<std::string> emit_singbox(const NodeList& nodes, const EmitOptions& opts,
                                 std::vector<std::string>* warnings = nullptr);

// --- XHTTP（见 src/emit/xhttp.cpp）---

/// 写进配置的 xhttp mode 是否被内核接受（空串=交给内核默认，算合法）。
[[nodiscard]] bool xhttp_mode_supported(const std::string& mode);

/// Xray 的 `xhttpSettings.downloadSettings`（StreamConfig 形态）。
[[nodiscard]] Json xhttp_download_settings_json(const ProxyNode& node);

/// Xray 的 `xhttpSettings`（含 `extra` 透传或离散的 `downloadSettings`）。
[[nodiscard]] Json xhttp_settings_json(const ProxyNode& node);

/// `extra=` / v2rayN `XhttpExtra` 用的 JSON 对象；没有可表达的高级参数时返回 null。
[[nodiscard]] Json xhttp_extra_json(const ProxyNode& node);

/// 只有 mihomo 目标要用：raw extra 里存在它表达不了的键（它没有 `extra` 概念）。
[[nodiscard]] bool xhttp_extra_has_untranslatable(const ProxyNode& node);

/// 分享链接输出形态。
enum class ShareMode {
  Links,   ///< 每行一条标准分享链接（通用）
  Base64,  ///< 上面那份列表的 base64（v2rayNG 的「订阅」内容）
  V2rayN,  ///< v2rayn://<协议小写>/<base64url(JSON)>，v2rayN / v2rayNG 专用；能承载 http 等无标准链接的协议
};

/// 分享链接输出。
/// warnings 非空时记录该目标的协议裁剪情况。
Result<std::string> emit_sharelinks(const NodeList& nodes, const EmitOptions& opts, ShareMode mode,
                                    std::vector<std::string>* warnings);

/// 生成单条标准分享链接；无法表示该协议时返回 nullopt。
[[nodiscard]] std::optional<std::string> build_share_link(const ProxyNode& node);

// --- 分流规则集（见 src/emit/rulesets.cpp）---

/// 按选中的规则集拼出 clash 的 `rules:` 列表（含最后的 `MATCH,<final_group>`）。
///
/// 顺序 = 界面上表格的顺序：**自定义规则在前**（按传入顺序，即表内从上到下），
/// 随后是内置规则集展开的规则（按目录顺序）。刻意不按 policy 重排 ——
/// 那样产物会和界面显示的顺序对不上，用户改顺序却看不到变化。
[[nodiscard]] std::vector<std::string> build_clash_rules(
    const std::vector<std::string>& selected, const std::string& final_group,
    const std::vector<CustomRuleSet>& custom, std::vector<std::string>* warnings);

// --- DNS 预设（见 src/emit/dns.cpp）---

/// 把选中的 DNS 预设 id / 字面地址展开成 `dns.nameserver` 的地址列表（去重、保序）。
/// ipv6 为真时一并带上各预设的 IPv6 地址。warnings 非空时记录无法识别的项。
[[nodiscard]] std::vector<std::string> resolve_nameservers(const std::vector<std::string>& selected,
                                                           bool ipv6,
                                                           std::vector<std::string>* warnings);

}  // namespace subconv
