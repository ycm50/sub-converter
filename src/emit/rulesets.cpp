// 分流规则集：把「直连本地 / 中国 / 伊朗 / Cloudflare …」这类可勾选的集合展开成 clash 规则。
//
// 设计取舍（都基于实测，不是照抄模板）：
//   * 只用 mihomo 自带的 GEOSITE / GEOIP 匹配，**不依赖任何远程 rule-provider**。
//     好处：生成的配置离线可用、能在本机用 `mihomo -t` 完整校验，也不存在运行时下载失败
//     导致整份配置起不来的风险（rule-providers 会在 mihomo 启动时去下载，断网即失败）。
//   * `mihomo -t` 会**校验 GEOSITE 类别是否存在**（不存在的会报
//     "list xxx not found in geosite.dat"），所以本文件的类别都是逐个跑 `mihomo -t` 验过的；
//     而 GEOIP 的国家码 `-t` 并不校验（`XX-BOGUS` 也能过），故 GEOIP 只用于通用国家码。
//   * 类别名有坑：伊朗在 v2fly 的 geosite 里叫 `category-ir`（没有 `ir`）。
//
// 规则顺序：clash 是首个命中生效，所以 REJECT 类必须排在 DIRECT 类之前，
// 否则广告域名一旦命中某个 DIRECT 规则就直接放行了。
//
// 自定义规则集（见 parse_custom_rule_sets）在这里与内置集合**一视同仁**：
// 同样参与「REJECT 先于 DIRECT」的重排，同样按目录顺序展开。

#include <string>
#include <string_view>
#include <vector>

#include "subconv/codec.hpp"
#include "subconv/convert.hpp"
#include "subconv/json.hpp"
#include "emit_internal.hpp"

namespace subconv {
namespace {

struct RuleSetDef {
  std::string_view id;
  std::string_view name;
  std::string_view policy;
  std::string_view note;
  std::vector<std::string_view> rules;
};

/// 目录表。展示顺序 = 这里定义的顺序；`rules:` 里的实际顺序见文件头注释。
const std::vector<RuleSetDef>& catalogue_defs() {
  static const std::vector<RuleSetDef> defs = {
      // 只留 GEOIP,LAN：mihomo 的 GEOIP 把 `lan` 当唯一伪规则（rules/common/geoip.go 的
      // NewGEOIP 里只有 `if country == "lan"`），而 isLan() 本身就是
      // ip.IsPrivate() || IsLoopback() || IsLinkLocalUnicast() || … —— 私有地址已经全覆盖。
      // `GEOIP,private` 会把 private 当国家码去 LoadGeoIPMatcher：mmdb 模式下永不命中，
      // geodata 模式下直接加载失败。与本文件「GEOIP 只用于通用国家码」的约定也自相矛盾。
      {"local", "直连本地", "DIRECT", "局域网 / 保留地址（GEOIP,LAN）",
       {"GEOIP,LAN,DIRECT,no-resolve"}},
      {"cn", "中国直连", "DIRECT", "中国域名与 IP（GEOSITE,cn + GEOIP,CN）",
       {"GEOSITE,cn,DIRECT", "GEOIP,CN,DIRECT"}},
      {"ir", "伊朗直连", "DIRECT", "伊朗域名与 IP（GEOSITE,category-ir + GEOIP,IR）",
       {"GEOSITE,category-ir,DIRECT", "GEOIP,IR,DIRECT"}},
      {"cloudflare", "Cloudflare 直连", "DIRECT", "Cloudflare 域名（GEOSITE,cloudflare）",
       {"GEOSITE,cloudflare,DIRECT"}},
      {"apple", "Apple 直连", "DIRECT", "Apple 域名（GEOSITE,apple）",
       {"GEOSITE,apple,DIRECT"}},
      {"microsoft", "微软直连", "DIRECT", "微软域名（GEOSITE,microsoft）",
       {"GEOSITE,microsoft,DIRECT"}},
      {"steam", "Steam 直连", "DIRECT", "Steam 域名（GEOSITE,steam）",
       {"GEOSITE,steam,DIRECT"}},
      {"onedrive", "OneDrive 直连", "DIRECT", "OneDrive 域名（GEOSITE,onedrive）",
       {"GEOSITE,onedrive,DIRECT"}},
      {"ads", "广告拦截", "REJECT", "广告域名直接拒绝（GEOSITE,category-ads-all）",
       {"GEOSITE,category-ads-all,REJECT"}},
  };
  return defs;
}

const RuleSetDef* find_def(std::string_view id) {
  for (const auto& def : catalogue_defs()) {
    if (def.id == id) return &def;
  }
  return nullptr;
}

bool already_selected(const std::vector<std::string>& ids, std::string_view id) {
  for (const auto& existing : ids) {
    if (existing == id) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// 自定义规则集
// ---------------------------------------------------------------------------

/// mihomo 认识的规则类型（区分大小写，内核按字面比较）。
///
/// 这里**不做白名单拦截**：`--ruleset-*` 允许写任何类型，交给 `mihomo -t` 去判。
/// 但常见类型单独列出来，用于两类场景：
///   1. 参数校验时把「明显拼错」的类型挑出来告警（例如 `DOMAIN-KEYWORDS`）；
///   2. 判断某类型是否需要 `no-resolve`（只有 IP 类规则才需要）。
/// 判断「是不是 IP 类」比「是不是合法类型」更重要 —— 它决定要不要补 `no-resolve`。
[[nodiscard]] bool is_ip_rule_type(std::string_view type) {
  return type == "IP-CIDR" || type == "IP-CIDR6" || type == "IP-SUFFIX" ||
         type == "GEOIP" || type == "IP-ASN" || type == "SRC-IP-CIDR" ||
         type == "SRC-IP-SUFFIX" || type == "SRC-GEOIP" || type == "SRC-IP-ASN";
}

/// 常见类型表 —— 只用于把「可能拼错」的类型提示出来，不构成白名单。
[[nodiscard]] bool is_known_rule_type(std::string_view type) {
  static const std::string_view kKnown[] = {
      "DOMAIN",      "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "DOMAIN-WILDCARD",
      "DOMAIN-REGEX", "GEOSITE",       "IP-CIDR",        "IP-CIDR6",
      "IP-SUFFIX",   "IP-ASN",         "GEOIP",          "SRC-GEOIP",
      "SRC-IP-CIDR", "SRC-IP-SUFFIX",  "SRC-IP-ASN",     "DST-PORT",
      "SRC-PORT",    "IN-PORT",        "IN-TYPE",        "IN-USER",
      "IN-NAME",     "PROCESS-PATH",   "PROCESS-PATH-WILDCARD", "PROCESS-PATH-REGEX",
      "PROCESS-NAME", "PROCESS-NAME-WILDCARD", "PROCESS-NAME-REGEX", "UID",
      "NETWORK",     "DSCP",           "RULE-SET",       "MATCH",
  };
  for (const auto& known : kKnown) {
    if (known == type) return true;
  }
  return false;
}

/// 规则值里不允许出现逗号：clash 的规则是 `TYPE,VALUE,POLICY` 三段式，
/// 值里带逗号会让内核把后半截当成策略名，产物直接加载失败。
/// 另外空白也会让行内解析出岔子，一并拒绝。
[[nodiscard]] bool value_is_usable(std::string_view value) {
  if (value.empty()) return false;
  for (const char c : value) {
    if (c == ',' || c == '\n' || c == '\r' || c == '\t') return false;
  }
  return true;
}

/// 策略名：`DIRECT` / `REJECT` / `REJECT-DROP` 等内置策略，或任意代理组名。
/// 用户名里同样不能有逗号（会把 `rules:` 那行切断），别的都放行
/// —— 组名是用户自己起的，可能带 emoji 与空格，没有理由拦。
[[nodiscard]] bool policy_is_usable(std::string_view policy) {
  if (policy.empty()) return false;
  for (const char c : policy) {
    if (c == ',' || c == '\n' || c == '\r') return false;
  }
  return true;
}

/// 把 `"DOMAIN-KEYWORD,ads"` 这种简写拆成 (type, value)；没有逗号则失败。
[[nodiscard]] bool split_shorthand(std::string_view text, std::string* type, std::string* value) {
  const std::size_t comma = text.find(',');
  if (comma == std::string_view::npos) return false;
  *type = codec::trim(text.substr(0, comma));
  *value = codec::trim(text.substr(comma + 1));
  return !type->empty() && !value->empty();
}

/// 解析单条规则（对象 `{"type":…,"value":…}` 或字符串简写 `"DOMAIN,a.com"`）。
/// 失败时把原因写进 `error`。
[[nodiscard]] bool parse_one_rule(const Json& item, std::string* out_type, std::string* out_value,
                                 std::string* error) {
  if (item.is_string()) {
    const std::string text = item.get<std::string>();
    if (!split_shorthand(text, out_type, out_value)) {
      *error = "规则写法不对：'" + text + "'（字符串简写要写成 \"TYPE,VALUE\"，例如 " +
               "\"DOMAIN-KEYWORD,ads\"）";
      return false;
    }
    return true;
  }
  if (!item.is_object()) {
    *error = "规则必须是对象 {\"type\":…,\"value\":…} 或字符串 \"TYPE,VALUE\"";
    return false;
  }
  // 类型：兼容 type / rule_type 两种写法
  if (item.contains("type")) {
    *out_type = codec::trim(item.at("type").is_string() ? item.at("type").get<std::string>()
                                                        : std::string());
  } else if (item.contains("rule_type")) {
    *out_type = codec::trim(item.at("rule_type").is_string()
                                ? item.at("rule_type").get<std::string>()
                                : std::string());
  }
  // 值：兼容 value / domain / keyword 三种写法，方便手写
  for (const char* key : {"value", "domain", "keyword", "payload"}) {
    if (item.contains(key) && item.at(key).is_string()) {
      *out_value = codec::trim(item.at(key).get<std::string>());
      if (!out_value->empty()) break;
    }
  }
  if (out_type->empty() || out_value->empty()) {
    *error = "规则缺少 type 或 value（也接受 value 写成 domain / keyword）";
    return false;
  }
  return true;
}

/// 从一份「规则集描述对象」里取出 id / name / policy / rules。
[[nodiscard]] bool parse_rule_set_object(const std::string& fallback_id, const Json& object,
                                         CustomRuleSet* out, std::string* error) {
  if (!object.is_object()) {
    *error = "规则集 '" + fallback_id + "' 应该是一个对象";
    return false;
  }
  out->id = fallback_id;
  if (object.contains("id") && object.at("id").is_string()) {
    const std::string id = codec::trim(object.at("id").get<std::string>());
    if (!id.empty()) out->id = id;
  }
  if (object.contains("name") && object.at("name").is_string()) {
    out->name = codec::trim(object.at("name").get<std::string>());
  }
  if (out->name.empty()) out->name = out->id;
  if (object.contains("policy") && object.at("policy").is_string()) {
    out->policy = codec::trim(object.at("policy").get<std::string>());
  }
  if (out->policy.empty()) out->policy = "DIRECT";

  if (!policy_is_usable(out->policy)) {
    *error = "规则集 '" + out->id + "' 的 policy 里不能有逗号或换行：" + out->policy;
    return false;
  }

  if (!object.contains("rules")) {
    *error = "规则集 '" + out->id + "' 缺少 rules 数组";
    return false;
  }
  const Json& rules = object.at("rules");
  // 也接受单条规则直接写成一个对象 / 字符串
  const bool single = rules.is_string() || rules.is_object();
  if (!single && !rules.is_array()) {
    *error = "规则集 '" + out->id + "' 的 rules 必须是数组（或单条规则）";
    return false;
  }
  auto push_rule = [&](const Json& item) -> bool {
    std::string type;
    std::string value;
    std::string why;
    if (!parse_one_rule(item, &type, &value, &why)) {
      *error = "规则集 '" + out->id + "' 里的规则有问题：" + why;
      return false;
    }
    if (!value_is_usable(value)) {
      *error = "规则集 '" + out->id + "' 的规则值里有逗号 / 空白 / 换行，内核无法解析：" + value;
      return false;
    }
    if (!is_known_rule_type(type)) {
      *error = "规则集 '" + out->id + "' 里的规则类型 '" + type +
               "' 不是常见类型；如果确实要用请确认拼写（内核会把未知类型当成加载错误）";
      return false;
    }
    out->rules.push_back(CustomRule{type, value});
    return true;
  };

  if (single) {
    if (!push_rule(rules)) return false;
  } else {
    for (const auto& item : rules) {
      if (!push_rule(item)) return false;
    }
  }
  if (out->rules.empty()) {
    *error = "规则集 '" + out->id + "' 里一条规则都没有";
    return false;
  }
  return true;
}

/// 把自定义规则集展开成 clash 规则行。
[[nodiscard]] std::vector<std::string> expand_custom(const CustomRuleSet& set) {
  std::vector<std::string> out;
  out.reserve(set.rules.size());
  for (const auto& rule : set.rules) {
    // IP 类规则补 no-resolve：不补的话 mihomo 会为了这条规则去解析域名，
    // 既拖慢首次匹配、也把 DNS 泄露给解析器 —— 与本项目「不依赖远程规则」的取向一致。
    const bool need_no_resolve = is_ip_rule_type(rule.type);
    // 用户自己写了 no-resolve / src 之类的附加参数就不再重复补
    const bool has_suffix = rule.value.find(',') != std::string::npos;
    std::string line = rule.type + "," + rule.value + "," + set.policy;
    if (need_no_resolve && !has_suffix) line += ",no-resolve";
    out.push_back(std::move(line));
  }
  return out;
}

}  // namespace

std::vector<RuleSetInfo> rule_set_catalogue() {
  std::vector<RuleSetInfo> out;
  out.reserve(catalogue_defs().size());
  for (const auto& def : catalogue_defs()) {
    RuleSetInfo info;
    info.id = std::string(def.id);
    info.name = std::string(def.name);
    info.policy = std::string(def.policy);
    info.note = std::string(def.note);
    info.custom = false;
    for (const auto& rule : def.rules) info.rules.emplace_back(rule);
    out.push_back(std::move(info));
  }
  return out;
}

Result<std::vector<CustomRuleSet>> parse_custom_rule_sets(std::string_view json) {
  const std::string text = codec::trim(json);
  if (text.empty()) return std::vector<CustomRuleSet>{};

  Json parsed;
  try {
    parsed = Json::parse(text);
  } catch (const Json::exception& e) {
    return fail(std::string("自定义规则集不是合法 JSON：") + e.what());
  }

  std::vector<CustomRuleSet> out;
  std::string error;

  if (parsed.is_array()) {
    // 形态 B：数组，id 写在元素里
    for (const auto& item : parsed) {
      std::string id;
      if (item.is_object() && item.contains("id") && item.at("id").is_string()) {
        id = codec::trim(item.at("id").get<std::string>());
      }
      if (id.empty()) return fail("自定义规则集数组里的每一项都必须有非空 id");
      CustomRuleSet set;
      if (!parse_rule_set_object(id, item, &set, &error)) return fail(error);
      out.push_back(std::move(set));
    }
  } else if (parsed.is_object()) {
    if (parsed.empty()) return std::vector<CustomRuleSet>{};
    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
      const std::string id = codec::trim(it.key());
      if (id.empty()) return fail("自定义规则集的 key 不能为空");
      CustomRuleSet set;
      if (!parse_rule_set_object(id, it.value(), &set, &error)) return fail(error);
      out.push_back(std::move(set));
    }
  } else {
    return fail("自定义规则集必须是对象（key 为 id）或数组");
  }

  // id 不能重复 —— 重复的 id 会让 rule_sets 选择变得有歧义
  for (std::size_t i = 0; i < out.size(); ++i) {
    for (std::size_t j = i + 1; j < out.size(); ++j) {
      if (out[i].id == out[j].id) {
        return fail("自定义规则集 id 重复：'" + out[i].id + "'");
      }
    }
    // 与内置集撞 id 同样有歧义（选择列表里分不出是哪个）
    if (find_def(out[i].id) != nullptr) {
      return fail("自定义规则集 id '" + out[i].id + "' 与内置规则集重名，请换一个名字");
    }
  }
  return out;
}

std::vector<std::string> default_rule_sets() { return {"local", "cn"}; }

std::vector<RuleTypeInfo> rule_type_catalogue() {
  // value 必须与 mihomo 文档里的类型名逐字符一致（内核按字面比较，大小写敏感）。
  // label / note 只是中文对照，绝不参与生成配置。
  return {
      {"DOMAIN", "精确域名", "只匹配完全相同的域名：DOMAIN,ads.example.com"},
      {"DOMAIN-SUFFIX", "域名后缀", "匹配该域名及其子域名：example.com 命中 www.example.com"},
      {"DOMAIN-KEYWORD", "域名关键字", "域名里含该关键字即命中：DOMAIN-KEYWORD,doubleclick"},
      {"DOMAIN-WILDCARD", "域名通配符", "仅支持 * 和 ?：DOMAIN-WILDCARD,*.google.com"},
      {"DOMAIN-REGEX", "域名正则", "正则匹配：DOMAIN-REGEX,^ads?\\."},
      {"GEOSITE", "内置域名库", "内核自带的分类域名库：GEOSITE,cn"},
      {"IP-CIDR", "IP 网段", "IPv4/IPv6 网段，自动补 no-resolve：IP-CIDR,10.0.0.0/8"},
      {"IP-CIDR6", "IP 网段（v6 写法）", "与 IP-CIDR 等价，只是写作 v6：IP-CIDR6,2001:db8::/32"},
      {"GEOIP", "IP 所属国家", "按 IP 归属地匹配：GEOIP,CN"},
      {"DST-PORT", "目标端口", "按目标端口匹配：DST-PORT,443"},
      {"PROCESS-NAME", "进程名", "按发起请求的进程匹配（安卓可填包名）：PROCESS-NAME,com.example"},
      {"NETWORK", "网络类型", "按 tcp / udp 匹配：NETWORK,udp"},
  };
}

std::vector<std::string> build_clash_rules(const std::vector<std::string>& selected,
                                           const std::string& final_group,
                                           const std::vector<CustomRuleSet>& custom,
                                           std::vector<std::string>* warnings) {
  // 输出顺序 = **界面上看到的顺序**：先自定义规则（按添加顺序 / 表内从上到下），
  // 再内置规则集展开的规则（按目录顺序）。
  //
  // 刻意**不再**按 policy 把 REJECT 提到最前：那会让产物和界面表格的顺序对不上，
  // 用户改一行顺序却看不到任何变化。表格就是唯一真源，产物照抄即可。
  // （先前那版把「非 REJECT 的自定义规则」整个挪到内置规则之后，导致自定义 DIRECT
  //   规则被 GEOSITE,cn 之类抢先命中而完全失效 —— 那是错的。）
  std::vector<std::string> rules;

  for (const auto& set : custom) {
    // **只要随请求一起给了自定义集就直接生效**，不要求它的 id 也出现在 selected 里：
    // 用户能把规则加进表格，就已经表达了"要用它"。再要求去别处勾选同名条目纯属多余，
    // 且极易出现"表里有规则、产物里却没有"的困惑（这是之前的真实缺陷）。
    for (const auto& line : expand_custom(set)) rules.push_back(line);
  }

  // 内置集：按目录顺序遍历，只留被选中的 —— 优先级由目录决定，与勾选先后无关。
  for (const auto& def : catalogue_defs()) {
    if (!already_selected(selected, def.id)) continue;
    for (const auto& rule : def.rules) rules.emplace_back(rule);
  }

  // 选中了但目录里没有的 id：去重后一次性告警，不影响其它规则
  std::vector<std::string> unknown;
  for (const auto& id : selected) {
    if (find_def(id) != nullptr) continue;
    bool is_custom = false;
    for (const auto& set : custom) {
      if (set.id == id) {
        is_custom = true;
        break;
      }
    }
    if (is_custom || already_selected(unknown, id)) continue;
    unknown.push_back(id);
  }

  if (warnings != nullptr && !unknown.empty()) {
    std::string available;
    for (const auto& def : catalogue_defs()) {
      if (!available.empty()) available += "、";
      available += std::string(def.id);
    }
    for (const auto& set : custom) {
      if (!available.empty()) available += "、";
      available += set.id;
    }
    std::string detail = "无法识别的规则集 ";
    for (std::size_t i = 0; i < unknown.size(); ++i) {
      if (i > 0) detail += "、";
      detail += "'" + unknown[i] + "'";
    }
    detail += "（已忽略；可用：" + available + "）";
    warnings->push_back(std::move(detail));
  }

  rules.push_back("MATCH," + final_group);
  return rules;
}

}  // namespace subconv

