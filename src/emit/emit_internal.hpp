// 各输出目标的内部声明
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "subconv/convert.hpp"

namespace subconv {

/// 去重 + 名称唯一化 + 排序（所有目标共用）。
[[nodiscard]] NodeList prepare_nodes(const NodeList& nodes, const EmitOptions& opts);

/// 生成配置首行注释。
[[nodiscard]] std::string config_header(std::string_view target, std::size_t node_count,
                                        std::string_view name = {});

// --- 具体目标 ---
Result<std::string> emit_clash(const NodeList& nodes, const EmitOptions& opts,
                               std::vector<std::string>* warnings = nullptr);
Result<std::string> emit_xray(const NodeList& nodes, const EmitOptions& opts);
Result<std::string> emit_singbox(const NodeList& nodes, const EmitOptions& opts);

/// 分享链接的输出形态。
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
/// REJECT 类规则永远排在 DIRECT 类之前；顺序即匹配优先级（clash 首个命中生效）。
/// warnings 非空时记录无法识别的规则集 id。
[[nodiscard]] std::vector<std::string> build_clash_rules(
    const std::vector<std::string>& selected, const std::string& final_group,
    std::vector<std::string>* warnings);

// --- DNS 预设（见 src/emit/dns.cpp）---

/// 把选中的 DNS 预设 id / 字面地址展开成 `dns.nameserver` 的地址列表（去重、保序）。
/// ipv6 为真时一并带上各预设的 IPv6 地址。warnings 非空时记录无法识别的项。
[[nodiscard]] std::vector<std::string> resolve_nameservers(const std::vector<std::string>& selected,
                                                           bool ipv6,
                                                           std::vector<std::string>* warnings);

}  // namespace subconv
