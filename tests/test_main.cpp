// subconv 轻量单元测试（无第三方框架）
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "subconv/codec.hpp"
#include "subconv/console.hpp"
#include "subconv/convert.hpp"
#include "subconv/fetch.hpp"
#include "subconv/json.hpp"
#include "subconv/server.hpp"
#include "subconv/vless_encryption.hpp"
#include "subconv/yaml.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_section = "";

void section(const char* name) {
  g_section = name;
  std::printf("== %s\n", name);
}

void check_impl(bool ok, const char* expr, int line) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL [%s] line %d: %s\n", g_section, line, expr);
  }
}

template <class A, class B>
void check_eq_impl(const A& a, const B& b, const char* ea, const char* eb, int line) {
  ++g_checks;
  if (!(a == b)) {
    ++g_failures;
    std::printf("  FAIL [%s] line %d: %s != %s\n", g_section, line, ea, eb);
  }
}

#define CHECK(cond) check_impl((cond), #cond, __LINE__)
#define CHECK_EQ(a, b) check_eq_impl((a), (b), #a, #b, __LINE__)

using subconv::Json;
using subconv::Yaml;
using namespace subconv::codec;

// ---------------------------------------------------------------------------
void test_base64() {
  section("base64");
  CHECK_EQ(base64_encode("hello"), std::string("aGVsbG8="));
  CHECK_EQ(base64_encode("a"), std::string("YQ=="));
  CHECK_EQ(base64_encode("ab"), std::string("YWI="));
  CHECK_EQ(base64_encode("abc"), std::string("YWJj"));

  // 二进制往返
  std::string blob;
  for (int i = 0; i < 256; ++i) blob.push_back(static_cast<char>(i));
  auto decoded = base64_decode(base64_encode(blob));
  CHECK(decoded.has_value());
  CHECK_EQ(*decoded, blob);

  // URL-safe 且无 padding
  const std::string raw = "\xfb\xff\xfe?>";
  auto url_safe = base64_encode_url(raw);
  CHECK(url_safe.find('+') == std::string::npos);
  CHECK(url_safe.find('/') == std::string::npos);
  auto back = base64_decode(url_safe);
  CHECK(back.has_value());
  CHECK_EQ(*back, raw);

  // 缺失 padding
  auto nopad = base64_decode("aGVsbG8");
  CHECK(nopad.has_value());
  CHECK_EQ(*nopad, std::string("hello"));

  // 含空白
  auto spaced = base64_decode("aGVs\nbG8=\r\n");
  CHECK(spaced.has_value());
  CHECK_EQ(*spaced, std::string("hello"));

  // 非法字符
  CHECK(!base64_decode("aGVs!bG8=").has_value());
  // 孤立半字节
  CHECK(!base64_decode("a").has_value());

  CHECK(looks_like_base64("aGVsbG8gd29ybGQ="));
  CHECK(!looks_like_base64("ss://abc"));
  CHECK(!looks_like_base64("短"));
}

void test_percent_and_uri() {
  section("percent / uri");
  CHECK_EQ(percent_decode("a%20b%2Fc"), std::string("a b/c"));
  CHECK_EQ(percent_decode("a+b"), std::string("a+b"));
  CHECK_EQ(percent_decode("a+b", true), std::string("a b"));
  CHECK_EQ(percent_decode("%E9%A6%99%E6%B8%AF"), std::string("香港"));
  CHECK_EQ(percent_encode("a b/c"), std::string("a%20b%2Fc"));

  auto uri = parse_uri("socks5://user:pass@1.2.3.4:1080#name");
  CHECK(uri.has_value());
  CHECK_EQ(uri->scheme, std::string("socks5"));
  CHECK_EQ(uri->userinfo, std::string("user:pass"));
  CHECK_EQ(uri->host, std::string("1.2.3.4"));
  CHECK_EQ(uri->port, static_cast<uint16_t>(1080));
  CHECK_EQ(uri->fragment, std::string("name"));

  auto v6 = parse_uri("http://[2001:db8::1]:8080/path?q=1");
  CHECK(v6.has_value());
  CHECK_EQ(v6->host, std::string("2001:db8::1"));
  CHECK_EQ(v6->port, static_cast<uint16_t>(8080));
  CHECK_EQ(v6->path, std::string("/path"));
  CHECK_EQ(v6->query, std::string("q=1"));

  CHECK(!parse_uri("no-scheme").has_value());
  CHECK(!parse_uri("http://host:99999").has_value());

  auto q = parse_query("a=1&b=x%20y&flag&empty=");
  CHECK_EQ(q["a"], std::string("1"));
  CHECK_EQ(q["b"], std::string("x y"));
  CHECK_EQ(q["flag"], std::string(""));
  CHECK_EQ(q.size(), static_cast<std::size_t>(4));
}

void test_ss() {
  section("ss:// 解析");
  const std::string secret = base64_encode("aes-256-gcm:password");

  auto sip002 = subconv::parse_node("ss://" + secret + "@1.2.3.4:8888#%E9%A6%99%E6%B8%AF");
  CHECK(sip002.has_value());
  if (sip002) {
    CHECK(sip002->protocol == subconv::Protocol::Shadowsocks);
    CHECK_EQ(sip002->cipher, std::string("aes-256-gcm"));
    CHECK_EQ(sip002->password, std::string("password"));
    CHECK_EQ(sip002->server, std::string("1.2.3.4"));
    CHECK_EQ(sip002->port, static_cast<uint16_t>(8888));
    CHECK_EQ(sip002->name, std::string("香港"));
  }

  auto legacy = subconv::parse_node(
      "ss://" + base64_encode("aes-128-gcm:pw@example.com:8388") + "#legacy");
  CHECK(legacy.has_value());
  if (legacy) {
    CHECK_EQ(legacy->cipher, std::string("aes-128-gcm"));
    CHECK_EQ(legacy->password, std::string("pw"));
    CHECK_EQ(legacy->server, std::string("example.com"));
    CHECK_EQ(legacy->port, static_cast<uint16_t>(8388));
    CHECK_EQ(legacy->name, std::string("legacy"));
  }

  // 密码含冒号
  auto colon = subconv::parse_node("ss://" + base64_encode("aes-256-gcm:a:b") + "@h:1#x");
  CHECK(colon.has_value());
  if (colon) {
    CHECK_EQ(colon->cipher, std::string("aes-256-gcm"));
    CHECK_EQ(colon->password, std::string("a:b"));
  }

  // 插件
  auto plugin = subconv::parse_node(
      "ss://" + secret +
      "@1.2.3.4:443/?plugin=obfs-local%3Bobfs%3Dhttp%3Bobfs-host%3Dwww.bing.com#p1");
  CHECK(plugin.has_value());
  if (plugin) {
    CHECK(plugin->plugin.present);
    CHECK_EQ(plugin->plugin.name, std::string("obfs-local"));
    CHECK_EQ(plugin->plugin.obfs_mode, std::string("http"));
    CHECK_EQ(plugin->plugin.obfs_host, std::string("www.bing.com"));
  }

  // 未编码 userinfo
  auto plain = subconv::parse_node("ss://aes-256-gcm:pw@h:1#p2");
  CHECK(plain.has_value());
  if (plain) CHECK_EQ(plain->cipher, std::string("aes-256-gcm"));

  // 错误路径
  CHECK(!subconv::parse_node("ss://!!!@h:1").has_value());
  CHECK(!subconv::parse_node("ss://" + secret + "@h").has_value());
  CHECK(!subconv::parse_node("ss://" + secret + "@h:0").has_value());
  CHECK(!subconv::parse_node("vmess://e30=").has_value());  // 尚未实现，应报错
  CHECK(!subconv::parse_node("unknown://x").has_value());
}

void test_socks_http() {
  section("socks5 / http");
  auto s = subconv::parse_node("socks5://u:p@1.1.1.1:1080#socks");
  CHECK(s.has_value());
  if (s) {
    CHECK(s->protocol == subconv::Protocol::Socks5);
    CHECK_EQ(s->username, std::string("u"));
    CHECK_EQ(s->password, std::string("p"));
    CHECK_EQ(s->name, std::string("socks"));
  }
  auto h = subconv::parse_node("https://1.1.1.1:443#tls-proxy");
  CHECK(h.has_value());
  if (h) {
    CHECK(h->protocol == subconv::Protocol::Http);
    CHECK(h->tls.enabled);
  }
}

void test_yaml() {
  section("yaml 输出");
  CHECK(Yaml::needs_quoting("11"));
  CHECK(Yaml::needs_quoting("1.5"));
  CHECK(Yaml::needs_quoting("true"));
  CHECK(Yaml::needs_quoting("null"));
  CHECK(Yaml::needs_quoting("2024-01-02"));
  CHECK(Yaml::needs_quoting("a: b"));
  CHECK(Yaml::needs_quoting("a #b"));
  CHECK(Yaml::needs_quoting("[x]"));
  CHECK(Yaml::needs_quoting(""));
  CHECK(Yaml::needs_quoting(" x"));
  CHECK(!Yaml::needs_quoting("abc"));
  CHECK(!Yaml::needs_quoting("DOMAIN-SUFFIX,cn,DIRECT"));
  CHECK(!Yaml::needs_quoting("香港节点"));
  CHECK(!Yaml::needs_quoting("aes-256-gcm"));
  CHECK_EQ(Yaml::quote("a\"b\\c"), std::string("\"a\\\"b\\\\c\""));

  Yaml root = Yaml::mapping();
  root.set("a", Yaml::integer(1));
  root.set("b", Yaml::scalar("x"));
  Yaml seq = Yaml::sequence();
  Yaml item = Yaml::mapping();
  item.set("k", Yaml::scalar("v"));
  item.set("n", Yaml::integer(2));
  seq.push(item);
  root.set("list", seq);
  CHECK_EQ(root.dump(), std::string("a: 1\nb: x\nlist:\n  - k: v\n    n: 2\n"));

  Yaml empty = Yaml::mapping();
  empty.set("e", Yaml::sequence());
  empty.set("m", Yaml::mapping());
  CHECK_EQ(empty.dump(), std::string("e: []\nm: {}\n"));

  // 重复键覆盖
  Yaml dup = Yaml::mapping();
  dup.set("k", Yaml::scalar("1st"));
  dup.set("k", Yaml::scalar("2nd"));
  CHECK_EQ(dup.pairs().size(), static_cast<std::size_t>(1));
  CHECK_EQ(dup.dump(), std::string("k: 2nd\n"));
}

void test_subscription() {
  section("订阅解析");
  const std::string a = "ss://" + base64_encode("aes-256-gcm:pw1") + "@a.example.com:443#A";
  const std::string b = "ss://" + base64_encode("aes-128-gcm:pw2") + "@b.example.com:8443#B";

  auto plain = subconv::parse_subscription(a + "\n" + b + "\n");
  CHECK(plain.has_value());
  if (plain) CHECK_EQ(plain->nodes.size(), static_cast<std::size_t>(2));

  // 整体 base64 包裹
  auto wrapped = subconv::parse_subscription(base64_encode(a + "\n" + b + "\n"));
  CHECK(wrapped.has_value());
  if (wrapped) CHECK_EQ(wrapped->nodes.size(), static_cast<std::size_t>(2));

  // 二次 base64
  auto twice = subconv::parse_subscription(base64_encode(base64_encode(a)));
  CHECK(twice.has_value());
  if (twice) CHECK_EQ(twice->nodes.size(), static_cast<std::size_t>(1));

  // 混杂注释与坏行
  auto mixed = subconv::parse_subscription("# 注释\n" + a + "\n随机文本\n" + b + "\n");
  CHECK(mixed.has_value());
  if (mixed) {
    CHECK_EQ(mixed->nodes.size(), static_cast<std::size_t>(2));
    CHECK_EQ(mixed->warnings.size(), static_cast<std::size_t>(1));
  }

  CHECK(!subconv::parse_subscription("").has_value());
  CHECK(!subconv::parse_subscription("完全不是订阅").has_value());
}

void test_clash_emit() {
  section("clash 输出");
  const std::string a = "ss://" + base64_encode("aes-256-gcm:pw1") + "@a.example.com:443#%E9%A6%99%E6%B8%AF01";
  const std::string b = "ss://" + base64_encode("aes-128-gcm:pw2") + "@b.example.com:8443#%E9%A6%99%E6%B8%AF01";
  const std::string c = "socks5://u:p@c.example.com:1080#SOCKS";

  auto sub = subconv::parse_subscription(a + "\n" + b + "\n" + c);
  CHECK(sub.has_value());
  if (!sub) return;

  subconv::EmitOptions opts;
  auto out = subconv::emit_config(sub->nodes, opts);
  CHECK(out.has_value());
  if (!out) return;

  const std::string& y = *out;
  CHECK(y.find("proxies:\n") != std::string::npos);
  CHECK(y.find("proxy-groups:\n") != std::string::npos);
  CHECK(y.find("rules:\n") != std::string::npos);
  CHECK(y.find("type: ss") != std::string::npos);
  CHECK(y.find("type: socks5") != std::string::npos);
  CHECK(y.find("MATCH,🐟 漏网之鱼") != std::string::npos);
  // 重名自动去重编号
  CHECK(y.find("香港01 2") != std::string::npos);
  CHECK(y.find("香港01") != std::string::npos);

  // 去重：完全相同的两条只保留一条
  auto dup = subconv::parse_subscription(a + "\n" + a);
  CHECK(dup.has_value());
  if (dup) {
    auto d = subconv::emit_config(dup->nodes, opts);
    CHECK(d.has_value());
    if (d) CHECK_EQ(d->find("a.example.com"), d->rfind("a.example.com"));
  }

  // 未知目标报错
  subconv::EmitOptions bad;
  bad.target = "nope";
  CHECK(!subconv::emit_config(sub->nodes, bad).has_value());
  bad.target = "surge";  // 规划中，尚未实现
  CHECK(!subconv::emit_config(sub->nodes, bad).has_value());
  bad.target = "mihomo";
  CHECK(subconv::emit_config(sub->nodes, bad).has_value());
  bad.target = "sing-box";
  CHECK(subconv::emit_config(sub->nodes, bad).has_value());
}

// ---------------------------------------------------------------------------
// 分流规则集：目录 + 展开进 rules 段的顺序
// ---------------------------------------------------------------------------
/// 取出 clash 输出里 rules: 段的标量值（`  - xxx` 里的 xxx）。
std::vector<std::string> rules_of(const std::string& yaml) {
  std::vector<std::string> out;
  const std::string marker = "\nrules:\n";
  const std::size_t pos = yaml.find(marker);
  if (pos == std::string::npos) return out;
  std::size_t i = pos + marker.size();
  while (i < yaml.size()) {
    std::size_t end = yaml.find('\n', i);
    if (end == std::string::npos) end = yaml.size();
    const std::string line = yaml.substr(i, end - i);
    if (line.rfind("  - ", 0) != 0) break;
    out.push_back(line.substr(4));
    i = end + 1;
  }
  return out;
}

void test_rulesets() {
  section("分流规则集");

  const auto catalogue = subconv::rule_set_catalogue();
  CHECK(catalogue.size() >= static_cast<std::size_t>(4));
  std::set<std::string> ids;
  for (const auto& rs : catalogue) {
    CHECK(!rs.id.empty());
    CHECK(!rs.name.empty());
    CHECK(rs.policy == "DIRECT" || rs.policy == "REJECT");
    CHECK(!rs.note.empty());
    CHECK(ids.insert(rs.id).second);  // id 必须唯一，否则命令行/界面会歧义
  }
  CHECK_EQ(ids.count("local"), static_cast<std::size_t>(1));
  CHECK_EQ(ids.count("cn"), static_cast<std::size_t>(1));
  CHECK_EQ(ids.count("ir"), static_cast<std::size_t>(1));
  CHECK_EQ(ids.count("cloudflare"), static_cast<std::size_t>(1));

  // 默认值必须是目录里真实存在的 id，否则默认输出就会带告警
  const auto defaults = subconv::default_rule_sets();
  CHECK(!defaults.empty());
  for (const auto& id : defaults) CHECK_EQ(ids.count(id), static_cast<std::size_t>(1));

  auto sub = subconv::parse_subscription("ss://YWVzLTI1Ni1nY206c3NwYXNz@ss.example.com:8443#SS\n");
  CHECK(sub.has_value());
  if (!sub) return;

  auto rules_with = [&sub](const std::vector<std::string>& sets) {
    subconv::EmitOptions opts;
    opts.target = "clash";
    opts.emoji = false;
    opts.rule_sets = sets;
    auto out = subconv::emit_config(sub->nodes, opts);
    CHECK(out.has_value());
    return out ? rules_of(*out) : std::vector<std::string>{};
  };

  // 默认（local + cn）：MATCH 永远收尾
  {
    const auto rules = rules_with(subconv::default_rule_sets());
    CHECK(!rules.empty());
    if (!rules.empty()) {
      CHECK_EQ(rules.front(), std::string("GEOIP,LAN,DIRECT,no-resolve"));
      CHECK_EQ(rules.back(), std::string("MATCH,漏网之鱼"));
      bool has_cn = false;
      for (const auto& r : rules) {
        if (r == "GEOSITE,cn,DIRECT") has_cn = true;
      }
      CHECK(has_cn);
    }
  }

  // 伊朗：v2fly 的 geosite 里叫 category-ir，没有 ir 这个类别
  {
    const auto rules = rules_with({"ir"});
    bool has_ir = false;
    for (const auto& r : rules) {
      if (r == "GEOSITE,category-ir,DIRECT") has_ir = true;
    }
    CHECK(has_ir);
    CHECK_EQ(rules.back(), std::string("MATCH,漏网之鱼"));
  }

  // REJECT 必须排在所有 DIRECT 之前，否则广告域名会先命中直连规则被放行
  {
    const auto rules = rules_with({"local", "cn", "ads"});
    CHECK(!rules.empty());
    if (!rules.empty()) {
      CHECK_EQ(rules.front(), std::string("GEOSITE,category-ads-all,REJECT"));
      std::size_t reject_at = rules.size();
      std::size_t direct_at = rules.size();
      for (std::size_t i = 0; i < rules.size(); ++i) {
        if (rules[i].find(",REJECT") != std::string::npos) reject_at = std::min(reject_at, i);
        if (rules[i].find(",DIRECT") != std::string::npos) direct_at = std::min(direct_at, i);
      }
      CHECK(reject_at < direct_at);
    }
  }

  // 选择顺序无关，重复 id 只展开一次
  {
    const auto a = rules_with({"local", "cn", "ir"});
    const auto b = rules_with({"ir", "cn", "local", "cn"});
    CHECK_EQ(a.size(), b.size());
    CHECK(a == b);
  }

  // 一个都不选 → 只剩 MATCH 兜底（合法配置）
  {
    const auto rules = rules_with({});
    CHECK_EQ(rules.size(), static_cast<std::size_t>(1));
    if (!rules.empty()) CHECK_EQ(rules.front(), std::string("MATCH,漏网之鱼"));
  }

  // 无法识别的 id：给告警但不失败，仍产出可用配置
  {
    subconv::EmitOptions opts;
    opts.target = "clash";
    opts.emoji = false;
    opts.rule_sets = {"cn", "没有这个规则集"};
    std::vector<std::string> warnings;
    auto out = subconv::emit_config(sub->nodes, opts, &warnings);
    CHECK(out.has_value());
    if (out) {
      CHECK(out->find("GEOSITE,cn,DIRECT") != std::string::npos);
      CHECK(out->find("MATCH,漏网之鱼") != std::string::npos);
    }
    const std::string joined = join(warnings, " | ");
    CHECK(joined.find("无法识别的规则集") != std::string::npos);
  }

  // 关掉 rules 时整个 rules 段都不出现
  {
    subconv::EmitOptions opts;
    opts.target = "clash";
    opts.include_rules = false;
    auto out = subconv::emit_config(sub->nodes, opts);
    CHECK(out.has_value());
    if (out) CHECK(out->find("\nrules:\n") == std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// DNS 预设 + ipv6 + 订阅名 → 文件名
// ---------------------------------------------------------------------------
void test_dns_and_name() {
  section("DNS / ipv6 / 订阅名");

  // ---- 目录 ----
  const auto catalogue = subconv::dns_catalogue();
  CHECK(catalogue.size() >= static_cast<std::size_t>(4));
  std::set<std::string> ids;
  for (const auto& preset : catalogue) {
    CHECK(!preset.id.empty());
    CHECK(!preset.name.empty());
    CHECK(preset.region == "国外" || preset.region == "国内");
    CHECK(!preset.v4.empty());  // 每个预设都得有 IPv4 地址，否则写出来是空解析器
    CHECK(ids.insert(preset.id).second);
  }
  CHECK_EQ(ids.count("cloudflare"), static_cast<std::size_t>(1));
  CHECK_EQ(ids.count("google"), static_cast<std::size_t>(1));
  CHECK_EQ(ids.count("alidns"), static_cast<std::size_t>(1));
  // 默认必须是真实存在的预设，且以国外为主
  const auto defaults = subconv::default_dns();
  CHECK(!defaults.empty());
  for (const auto& id : defaults) {
    CHECK_EQ(ids.count(id), static_cast<std::size_t>(1));
    const auto it = std::find_if(catalogue.begin(), catalogue.end(),
                                 [&id](const subconv::DnsPresetInfo& p) { return p.id == id; });
    CHECK(it != catalogue.end());
    if (it != catalogue.end()) CHECK_EQ(it->region, std::string("国外"));
  }

  auto sub = subconv::parse_subscription("ss://YWVzLTI1Ni1nY206c3NwYXNz@ss.example.com:8443#SS\n");
  CHECK(sub.has_value());
  if (!sub) return;

  auto dns_lines = [&sub](const std::vector<std::string>& dns, bool ipv6,
                          std::vector<std::string>* warnings = nullptr) {
    subconv::EmitOptions opts;
    opts.target = "clash";
    opts.emoji = false;
    opts.dns = dns;
    opts.ipv6 = ipv6;
    auto out = subconv::emit_config(sub->nodes, opts, warnings);
    CHECK(out.has_value());
    std::vector<std::string> lines;
    if (!out) return lines;
    const std::string marker = "\n  nameserver:\n";
    const std::size_t pos = out->find(marker);
    if (pos == std::string::npos) return lines;
    std::size_t i = pos + marker.size();
    while (i < out->size()) {
      std::size_t end = out->find('\n', i);
      if (end == std::string::npos) end = out->size();
      const std::string line = out->substr(i, end - i);
      if (line.rfind("    - ", 0) != 0) break;
      lines.push_back(line.substr(6));
      i = end + 1;
    }
    return lines;
  };

  // 默认两组国外解析器：ipv6 开 → IPv4 + IPv6 都在
  {
    const auto lines = dns_lines(subconv::default_dns(), true);
    CHECK(!lines.empty());
    CHECK(std::find(lines.begin(), lines.end(), "1.1.1.1") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "8.8.8.8") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "2606:4700:4700::1111") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "2001:4860:4860::8888") != lines.end());
  }

  // ipv6 关 → 只留 IPv4
  {
    const auto lines = dns_lines(subconv::default_dns(), false);
    CHECK(std::find(lines.begin(), lines.end(), "1.1.1.1") != lines.end());
    CHECK(std::find(lines.begin(), lines.end(), "2606:4700:4700::1111") == lines.end());
    for (const auto& line : lines) CHECK(line.find(':') == std::string::npos);
  }

  // 字面地址（IPv4 / IPv6 / DoH URL）原样透传，去重且保序
  {
    const auto lines = dns_lines({"1.1.1.1", "https://8.8.8.8/dns-query", "1.1.1.1",
                                 "2606:4700:4700::1111"},
                                true);
    CHECK_EQ(lines.size(), static_cast<std::size_t>(3));
    if (lines.size() == 3) {
      CHECK_EQ(lines[0], std::string("1.1.1.1"));
      CHECK_EQ(lines[1], std::string("https://8.8.8.8/dns-query"));
      CHECK_EQ(lines[2], std::string("2606:4700:4700::1111"));
    }
  }

  // 拼错的预设 id 不能当成解析器写进配置（mihomo 会因此拒绝加载），要告警并跳过
  {
    std::vector<std::string> warnings;
    const auto lines = dns_lines({"cloudflar", "1.1.1.1"}, true, &warnings);
    CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
    if (!lines.empty()) CHECK_EQ(lines[0], std::string("1.1.1.1"));
    const std::string joined = join(warnings, " | ");
    CHECK(joined.find("无法识别的 DNS") != std::string::npos);
    CHECK(joined.find("cloudflar") != std::string::npos);
  }

  // 空列表 → 不写 nameserver（但 dns 段其它字段还在）
  {
    subconv::EmitOptions opts;
    opts.target = "clash";
    opts.dns.clear();
    auto out = subconv::emit_config(sub->nodes, opts);
    CHECK(out.has_value());
    if (out) {
      CHECK(out->find("nameserver") == std::string::npos);
      CHECK(out->find("enhanced-mode: redir-host") != std::string::npos);
    }
  }

  // ipv6 开关同时落到根节点与 dns 段
  {
    for (const bool ipv6 : {true, false}) {
      subconv::EmitOptions opts;
      opts.target = "clash";
      opts.emoji = false;
      opts.ipv6 = ipv6;
      auto out = subconv::emit_config(sub->nodes, opts);
      CHECK(out.has_value());
      if (!out) continue;
      const std::string expected = ipv6 ? "ipv6: true" : "ipv6: false";
      CHECK(out->find(expected) != std::string::npos);
      // 根节点一处 + dns 段一处
      std::size_t count = 0;
      for (std::size_t pos = out->find(expected); pos != std::string::npos;
           pos = out->find(expected, pos + 1)) {
        ++count;
      }
      CHECK_EQ(count, static_cast<std::size_t>(2));
    }
  }

  // 订阅名：出现在配置首行注释里；没给就不写 name=
  {
    subconv::EmitOptions opts;
    opts.target = "clash";
    opts.emoji = false;
    opts.filename = "我的机场";
    auto out = subconv::emit_config(sub->nodes, opts);
    CHECK(out.has_value());
    if (out) CHECK(out->find("| name=我的机场") != std::string::npos);

    opts.filename.clear();
    auto plain = subconv::emit_config(sub->nodes, opts);
    CHECK(plain.has_value());
    if (plain) CHECK(plain->find("| name=") == std::string::npos);
  }

  // 订阅名 → 下载文件名
  CHECK_EQ(subconv::server::filename_for("", "clash"), std::string("subconv-clash.yaml"));
  CHECK_EQ(subconv::server::filename_for("   ", "clash"), std::string("subconv-clash.yaml"));
  CHECK_EQ(subconv::server::filename_for("我的机场", "clash"), std::string("我的机场.yaml"));
  CHECK_EQ(subconv::server::filename_for("我的机场", "xray"), std::string("我的机场.json"));
  CHECK_EQ(subconv::server::filename_for("我的机场", "links"), std::string("我的机场.txt"));
  CHECK_EQ(subconv::server::filename_for("我的机场", "v2rayn"), std::string("我的机场.txt"));
  // 自带扩展名就尊重它，且 "v1.2" 这种不算扩展名
  CHECK_EQ(subconv::server::filename_for("my.json", "clash"), std::string("my.json"));
  CHECK_EQ(subconv::server::filename_for("机场 v1.2", "clash"), std::string("机场 v1.2.yaml"));
  // 路径分隔符与 Windows 非法字符要被剔掉，别把订阅名变成路径穿越
  CHECK_EQ(subconv::server::filename_for("../../etc/passwd", "clash"),
           std::string("....etcpasswd.yaml"));
  CHECK_EQ(subconv::server::filename_for("a\\b:c*d?e\"f<g>h|i", "clash"), std::string("abcdefghi.yaml"));
  CHECK_EQ(subconv::server::filename_for("....", "clash"), std::string("subconv-clash.yaml"));
}

// ---------------------------------------------------------------------------
void test_advanced_protocols() {
  section("高级协议解析");
  const std::string uuid = "b831381d-6324-4d53-ad4f-8cda48b30811";

  // --- SSR ---
  const std::string ssr_inner =
      "ssr.example.com:8388:auth_aes128_md5:aes-256-cfb:http_simple:" +
      base64_encode_url("ssrpass") + "/?obfsparam=" + base64_encode_url("www.bing.com") +
      "&protoparam=" + base64_encode_url("12345") + "&remarks=" + base64_encode_url("SSR节点");
  auto ssr = subconv::parse_node("ssr://" + base64_encode_url(ssr_inner));
  CHECK(ssr.has_value());
  if (ssr) {
    CHECK(ssr->protocol == subconv::Protocol::ShadowsocksR);
    CHECK_EQ(ssr->server, std::string("ssr.example.com"));
    CHECK_EQ(ssr->port, static_cast<uint16_t>(8388));
    CHECK_EQ(ssr->cipher, std::string("aes-256-cfb"));
    CHECK_EQ(ssr->password, std::string("ssrpass"));
    CHECK_EQ(ssr->ssr_protocol, std::string("auth_aes128_md5"));
    CHECK_EQ(ssr->ssr_obfs, std::string("http_simple"));
    CHECK_EQ(ssr->ssr_obfs_param, std::string("www.bing.com"));
    CHECK_EQ(ssr->name, std::string("SSR节点"));
  }
  CHECK(!subconv::parse_node("ssr://" + base64_encode_url("a:b:c")).has_value());

  // --- VMess: base64(JSON) ---
  const std::string vmess_json =
      R"({"v":"2","ps":"V","add":"vm.example.com","port":"443","id":")" + uuid +
      R"(","aid":"0","scy":"auto","net":"ws","type":"none","host":"h.example.com","path":"/p","tls":"tls","sni":"s.example.com","alpn":"h2,http/1.1","fp":"chrome"})";
  auto vm = subconv::parse_node("vmess://" + base64_encode(vmess_json));
  CHECK(vm.has_value());
  if (vm) {
    CHECK(vm->protocol == subconv::Protocol::Vmess);
    CHECK_EQ(vm->server, std::string("vm.example.com"));
    CHECK_EQ(vm->port, static_cast<uint16_t>(443));
    CHECK_EQ(vm->uuid, uuid);
    CHECK_EQ(vm->cipher, std::string("auto"));
    CHECK(vm->network == subconv::Network::Ws);
    CHECK(vm->tls.enabled);
    CHECK_EQ(vm->tls.sni, std::string("s.example.com"));
    CHECK_EQ(vm->ws.path, std::string("/p"));
    CHECK_EQ(vm->ws.host, std::string("h.example.com"));
    CHECK_EQ(vm->tls.client_fingerprint, std::string("chrome"));
    CHECK_EQ(vm->tls.alpn.size(), static_cast<std::size_t>(2));
  }
  // 数字型 port / aid
  const std::string vmess_num = R"({"v":"2","ps":"N","add":"n.example.com","port":8443,"id":")" +
                                uuid +
                                R"(","aid":64,"scy":"aes-128-gcm","net":"tcp"})";
  auto vn = subconv::parse_node("vmess://" + base64_encode(vmess_num));
  CHECK(vn.has_value());
  if (vn) {
    CHECK_EQ(vn->port, static_cast<uint16_t>(8443));
    CHECK_EQ(vn->alter_id, 64);
    CHECK(vn->network == subconv::Network::Tcp);
    CHECK(!vn->tls.enabled);
  }
  // vmess1 变体：cipher:uuid@host:port
  auto v1 =
      subconv::parse_node("vmess://" + base64_encode("auto:" + uuid + "@v1.example.com:8080"));
  CHECK(v1.has_value());
  if (v1) {
    CHECK(v1->protocol == subconv::Protocol::Vmess);
    CHECK_EQ(v1->server, std::string("v1.example.com"));
    CHECK_EQ(v1->uuid, uuid);
  }

  // --- VLESS + REALITY ---
  auto vl = subconv::parse_node(
      "vless://" + uuid +
      "@vl.example.com:443?encryption=none&security=reality&sni=www.microsoft.com&fp=chrome"
      "&pbk=ABCDEF123456&sid=0123abcd&type=tcp&flow=xtls-rprx-vision#VL");
  CHECK(vl.has_value());
  if (vl) {
    CHECK(vl->protocol == subconv::Protocol::Vless);
    CHECK(vl->tls.enabled);
    CHECK(vl->tls.reality);
    CHECK_EQ(vl->tls.reality_public_key, std::string("ABCDEF123456"));
    CHECK_EQ(vl->tls.reality_short_id, std::string("0123abcd"));
    CHECK_EQ(vl->flow, std::string("xtls-rprx-vision"));
    CHECK(vl->network == subconv::Network::Tcp);
  }
  // tcp + headerType=http 应落到 HTTP 传输层
  auto vlh = subconv::parse_node(
      "vless://" + uuid + "@v.example.com:80?type=tcp&headerType=http&path=/p&host=h.com&security=none#H");
  CHECK(vlh.has_value());
  if (vlh) {
    CHECK(vlh->network == subconv::Network::Http);
    CHECK(!vlh->tls.enabled);
    CHECK_EQ(vlh->h2.path, std::string("/p"));
  }
  // grpc + serviceName
  auto vlg = subconv::parse_node("vless://" + uuid + "@g.example.com:443?type=grpc&serviceName=svc&security=tls&sni=g.example.com#G");
  CHECK(vlg.has_value());
  if (vlg) {
    CHECK(vlg->network == subconv::Network::Grpc);
    CHECK_EQ(vlg->grpc.service_name, std::string("svc"));
  }

  // --- Trojan ---
  auto tj = subconv::parse_node(
      "trojan://pw@tj.example.com:443?sni=tj.example.com&type=ws&path=%2Ftjws&host=tj.example.com#TJ");
  CHECK(tj.has_value());
  if (tj) {
    CHECK(tj->protocol == subconv::Protocol::Trojan);
    CHECK(tj->tls.enabled);
    CHECK_EQ(tj->tls.sni, std::string("tj.example.com"));
    CHECK(tj->network == subconv::Network::Ws);
    CHECK_EQ(tj->ws.path, std::string("/tjws"));
  }

  // --- Hysteria2 ---
  auto h2 = subconv::parse_node(
      "hysteria2://hypw@hy2.example.com:443?sni=hy2.example.com&insecure=1&obfs=salamander"
      "&obfs-password=op&up=100&down=200#H2");
  CHECK(h2.has_value());
  if (h2) {
    CHECK(h2->protocol == subconv::Protocol::Hysteria2);
    CHECK_EQ(h2->password, std::string("hypw"));
    CHECK_EQ(h2->obfs, std::string("salamander"));
    CHECK_EQ(h2->obfs_password, std::string("op"));
    CHECK_EQ(h2->up, std::string("100"));
    CHECK(h2->tls.insecure);
  }
  CHECK(subconv::parse_node("hy2://pw@h.example.com:443#x").has_value());

  // --- Hysteria v1 ---
  auto h1 = subconv::parse_node(
      "hysteria://hy.example.com:443?auth=apw&peer=hy.example.com&insecure=1&protocol=udp&upmbps=100#H1");
  CHECK(h1.has_value());
  if (h1) {
    CHECK(h1->protocol == subconv::Protocol::Hysteria);
    CHECK_EQ(h1->password, std::string("apw"));
    CHECK_EQ(h1->tls.sni, std::string("hy.example.com"));
    CHECK_EQ(h1->up, std::string("100"));
  }

  // --- TUIC ---
  auto tc = subconv::parse_node(
      "tuic://" + uuid +
      ":tpw@tuic.example.com:443?congestion_control=bbr&alpn=h3&udp_relay_mode=native#T");
  CHECK(tc.has_value());
  if (tc) {
    CHECK(tc->protocol == subconv::Protocol::Tuic);
    CHECK_EQ(tc->uuid, uuid);
    CHECK_EQ(tc->password, std::string("tpw"));
    CHECK_EQ(tc->congestion_control, std::string("bbr"));
    CHECK(tc->tls.enabled);
  }

  // --- Snell ---
  auto sn = subconv::parse_node(
      "snell://psk123@sn.example.com:443?version=4&obfs=http&obfs-host=www.bing.com#S");
  CHECK(sn.has_value());
  if (sn) {
    CHECK(sn->protocol == subconv::Protocol::Snell);
    CHECK_EQ(sn->password, std::string("psk123"));
    CHECK_EQ(sn->version, 4);
    CHECK_EQ(sn->obfs, std::string("http"));
  }

  // --- 错误路径 ---
  CHECK(!subconv::parse_node("vless://@h:443").has_value());
  CHECK(!subconv::parse_node("trojan://h:443").has_value());
  CHECK(!subconv::parse_node("vmess://" + base64_encode("not json")).has_value());
  CHECK(!subconv::parse_node("wireguard://x@h:1").has_value());
  CHECK(!subconv::parse_node("hy2://pw@h:443").has_value() == false);  // hy2 合法
}

// ---------------------------------------------------------------------------
void test_emit_targets() {
  section("xray / sing-box 输出");
  const std::string uuid = "b831381d-6324-4d53-ad4f-8cda48b30811";
  const std::vector<std::string> lines = {
      "ss://" + base64_encode("aes-256-gcm:pw") + "@ss.example.com:8388#SS",
      "ss://" + base64_encode("aes-256-cfb:pw") + "@ss-old.example.com:8388#SS-LEGACY",
      "ssr://" + base64_encode_url(
                     "s.example.com:8388:origin:aes-256-cfb:plain:" + base64_encode_url("pw")),
      "vless://" + uuid +
          "@vl.example.com:443?encryption=none&security=reality&sni=www.microsoft.com"
          "&pbk=KEY&sid=ab&type=tcp&flow=xtls-rprx-vision#VL",
      "trojan://pw@tj.example.com:443?sni=tj.example.com#TJ",
      "hysteria2://pw@h2.example.com:443?sni=h2.example.com&insecure=1#H2",
      "tuic://" + uuid + ":pw@t.example.com:443?sni=t.example.com#T",
      "snell://psk@sn.example.com:443?version=4#S",
  };
  std::string fixture;
  for (const auto& line : lines) fixture += line + "\n";

  auto sub = subconv::parse_subscription(fixture);
  CHECK(sub.has_value());
  if (!sub) return;
  CHECK_EQ(sub->nodes.size(), lines.size());

  // --- Xray ---
  subconv::EmitOptions xo;
  xo.target = "xray";
  auto xr = subconv::emit_config(sub->nodes, xo);
  CHECK(xr.has_value());
  if (xr) {
    Json j = Json::parse(*xr, nullptr, false);
    CHECK(!j.is_discarded());
    CHECK(j.contains("outbounds"));
    CHECK(j.contains("routing"));
    CHECK_EQ(j["routing"]["balancers"][0]["strategy"]["type"], std::string("leastPing"));

    int ss_count = 0;
    int ssr_count = 0;
    bool found_reality = false;
    for (const auto& o : j["outbounds"]) {
      if (o["protocol"] == "shadowsocks") ++ss_count;
      if (o["protocol"] == "ssr") ++ssr_count;
      if (o.contains("streamSettings") && o["streamSettings"].contains("realitySettings")) {
        found_reality = true;
        CHECK_EQ(o["streamSettings"]["realitySettings"]["publicKey"], std::string("KEY"));
      }
    }
    // 仅 AEAD 加密的 ss 保留；ssr 不被 Xray 支持
    CHECK_EQ(ss_count, 1);
    CHECK_EQ(ssr_count, 0);
    CHECK(found_reality);
    // Xray 已移除 allowInsecure，必须用 verifyPeerCertByName
    CHECK(xr->find("allowInsecure") == std::string::npos);
  }

  // --- sing-box ---
  subconv::EmitOptions so;
  so.target = "singbox";
  auto sb = subconv::emit_config(sub->nodes, so);
  CHECK(sb.has_value());
  if (sb) {
    Json j = Json::parse(*sb, nullptr, false);
    CHECK(!j.is_discarded());
    CHECK_EQ(j["outbounds"][0]["type"], std::string("selector"));
    CHECK_EQ(j["outbounds"][1]["type"], std::string("urltest"));
    std::set<std::string> types;
    for (const auto& o : j["outbounds"]) types.insert(o["type"].get<std::string>());
    CHECK(types.count("shadowsocks") == 1);
    CHECK(types.count("hysteria2") == 1);
    CHECK(types.count("tuic") == 1);
    CHECK(types.count("ssr") == 0);    // sing-box 已移除 SSR
    CHECK(types.count("snell") == 0);  // sing-box 不支持 Snell
    CHECK(j["route"].contains("final"));
  }

  // --- clash legacy：meta 独有协议应被剔除并给出告警 ---
  subconv::EmitOptions lo;
  lo.target = "clash";
  lo.clash_legacy = true;
  auto lg = subconv::emit_config(sub->nodes, lo);
  CHECK(lg.has_value());
  if (lg) {
    CHECK(lg->find("type: vless") == std::string::npos);
    CHECK(lg->find("type: ssr") == std::string::npos);
    CHECK(lg->find("type: hysteria2") == std::string::npos);
    CHECK(lg->find("type: ss\n") != std::string::npos);
    CHECK(lg->find("告警") != std::string::npos);
  }

  // --- clash meta：全协议 ---
  subconv::EmitOptions mo;
  mo.target = "clash";
  auto mg = subconv::emit_config(sub->nodes, mo);
  CHECK(mg.has_value());
  if (mg) {
    CHECK(mg->find("reality-opts:") != std::string::npos);
    CHECK(mg->find("type: hysteria2") != std::string::npos);
    CHECK(mg->find("type: snell") != std::string::npos);
    CHECK(mg->find("obfs: plain") != std::string::npos);   // SSR 的 obfs 字段
    CHECK(mg->find("psk: psk") != std::string::npos);      // Snell
  }
}

// ---------------------------------------------------------------------------
void test_sniff_and_userinfo() {
  section("内容嗅探 / userinfo");
  using subconv::fetch::ContentKind;

  CHECK(subconv::fetch::sniff_content("ss://a@h:1#x\nss://b@h:2#y\n") ==
        ContentKind::ShareLinks);
  CHECK(subconv::fetch::sniff_content("   \n  ss://a@h:1#x") == ContentKind::ShareLinks);
  CHECK(subconv::fetch::sniff_content("<html><body>err</body></html>") == ContentKind::Html);
  CHECK(subconv::fetch::sniff_content("<!DOCTYPE html>\n<html>") == ContentKind::Html);
  CHECK(subconv::fetch::sniff_content("\xEF\xBB\xBF<html>") == ContentKind::Html);
  CHECK(subconv::fetch::sniff_content("mixed-port: 7890\nproxies:\n  - name: a\n") ==
        ContentKind::ClashYaml);
  CHECK(subconv::fetch::sniff_content("proxy-groups:\n  - name: a\n") ==
        ContentKind::ClashYaml);
  CHECK(subconv::fetch::sniff_content(R"({"outbounds":[]})") == ContentKind::JsonConfig);
  CHECK(subconv::fetch::sniff_content("[]") == ContentKind::JsonConfig);
  // 但 JSON 版 Clash 配置（mihomo 直接支持，BPB 面板 ?app=clash 返回的就是它）要走 Clash 解析
  CHECK(subconv::fetch::sniff_content(
            R"({"mixed-port":7890,"proxies":[{"name":"a","type":"ss"}]})") ==
        ContentKind::ClashYaml);
  CHECK(subconv::fetch::sniff_content(
            R"({"proxies":[{"name":"a","type":"trojan","password":"p"}]})") ==
        ContentKind::ClashYaml);
  CHECK(subconv::fetch::sniff_content(R"({"proxy-groups":[{"name":"g","type":"select"}]})") ==
        ContentKind::ClashYaml);
  // proxies 段可以排在几千字节的前置配置之后（判断窗口不能只有 4KB）
  CHECK(subconv::fetch::sniff_content(
            R"({"mixed-port":7890,"dns":{"nameserver":["1.1.1.1"]},"pad":")" +
            std::string(5000, 'x') + R"(","proxies":[]})") == ContentKind::ClashYaml);
  // sing-box / Xray 的 JSON 不能因为含 proxy 字样的键就被当成 Clash
  CHECK(subconv::fetch::sniff_content(
            R"({"log":{"level":"info"},"outbounds":[{"type":"direct","tag":"direct"}]})") ==
        ContentKind::JsonConfig);
  CHECK(subconv::fetch::sniff_content("") == ContentKind::Unknown);
  // 整体 Base64 包裹的订阅
  CHECK(subconv::fetch::sniff_content(
            base64_encode("ss://a@h:1#x\nss://b@h:2#y\n")) == ContentKind::ShareLinks);

  auto info = subconv::fetch::parse_userinfo(
      "upload=1024; download=2048; total=1048576; expire=1735689600");
  CHECK_EQ(info.upload, static_cast<int64_t>(1024));
  CHECK_EQ(info.download, static_cast<int64_t>(2048));
  CHECK_EQ(info.total, static_cast<int64_t>(1048576));
  CHECK_EQ(info.expire, static_cast<int64_t>(1735689600));
  CHECK(info.has_any());
  CHECK(!subconv::fetch::parse_userinfo("garbage").has_any());
  CHECK(!subconv::fetch::parse_userinfo("").has_any());
  CHECK_EQ(subconv::fetch::parse_userinfo("total=abc").total, static_cast<int64_t>(0));

  CHECK(subconv::fetch::is_url("https://a.com/x"));
  CHECK(subconv::fetch::is_url("HTTP://a.com"));
  CHECK(!subconv::fetch::is_url("./local.txt"));
}

// ---------------------------------------------------------------------------
void test_clash_yaml_input() {
  section("Clash YAML 作为输入源");
  const std::string yaml = R"(
mixed-port: 7890
proxies:
  - name: SS1
    type: ss
    server: a.example.com
    port: 8443
    cipher: aes-256-gcm
    password: pw
    plugin: obfs
    plugin-opts:
      mode: http
      host: www.bing.com
  - name: VM1
    type: vmess
    server: b.example.com
    port: 443
    uuid: b831381d-6324-4d53-ad4f-8cda48b30811
    alterId: 64
    cipher: auto
    tls: true
    servername: b.example.com
    network: ws
    ws-opts:
      path: /vmws
      headers:
        Host: b.example.com
    skip-cert-verify: true
    alpn: [h2, http/1.1]
  - name: VL1
    type: vless
    server: c.example.com
    port: 443
    uuid: b831381d-6324-4d53-ad4f-8cda48b30811
    flow: xtls-rprx-vision
    tls: true
    servername: www.microsoft.com
    reality-opts:
      public-key: KEY123
      short-id: abcd
    client-fingerprint: chrome
  - name: H2
    type: hysteria2
    server: d.example.com
    port: 443
    password: pw2
    obfs: salamander
    obfs-password: op
    sni: d.example.com
    skip-cert-verify: true
    up: 100 Mbps
    down: 200 Mbps
  - name: BAD
    type: unknown-type
    server: e.example.com
    port: 443
)";
  auto sub = subconv::parse_clash_yaml(yaml, "test");
  CHECK(sub.has_value());
  if (!sub) return;
  CHECK_EQ(sub->nodes.size(), static_cast<std::size_t>(4));
  CHECK_EQ(sub->warnings.size(), static_cast<std::size_t>(1));

  const auto& ss = sub->nodes[0];
  CHECK(ss.protocol == subconv::Protocol::Shadowsocks);
  CHECK_EQ(ss.cipher, std::string("aes-256-gcm"));
  CHECK(ss.plugin.present);
  CHECK_EQ(ss.plugin.obfs_mode, std::string("http"));
  CHECK_EQ(ss.plugin.obfs_host, std::string("www.bing.com"));

  const auto& vm = sub->nodes[1];
  CHECK(vm.protocol == subconv::Protocol::Vmess);
  CHECK_EQ(vm.alter_id, 64);
  CHECK(vm.network == subconv::Network::Ws);
  CHECK_EQ(vm.ws.path, std::string("/vmws"));
  CHECK_EQ(vm.ws.host, std::string("b.example.com"));
  CHECK(vm.tls.enabled);
  CHECK(vm.tls.insecure);
  CHECK_EQ(vm.tls.sni, std::string("b.example.com"));
  CHECK_EQ(vm.tls.alpn.size(), static_cast<std::size_t>(2));

  const auto& vl = sub->nodes[2];
  CHECK(vl.protocol == subconv::Protocol::Vless);
  CHECK(vl.tls.reality);
  CHECK_EQ(vl.tls.reality_public_key, std::string("KEY123"));
  CHECK_EQ(vl.tls.reality_short_id, std::string("abcd"));
  CHECK_EQ(vl.flow, std::string("xtls-rprx-vision"));
  CHECK_EQ(vl.tls.client_fingerprint, std::string("chrome"));

  const auto& h2 = sub->nodes[3];
  CHECK(h2.protocol == subconv::Protocol::Hysteria2);
  CHECK_EQ(h2.up, std::string("100 Mbps"));
  CHECK_EQ(h2.obfs, std::string("salamander"));

  // up 带单位也要能在 sing-box 目标里解析成数字
  subconv::EmitOptions so;
  so.target = "singbox";
  auto sb = subconv::emit_config(sub->nodes, so);
  CHECK(sb.has_value());
  if (sb) {
    const Json j = Json::parse(*sb, nullptr, false);
    bool found = false;
    for (const auto& o : j["outbounds"]) {
      if (o.value("tag", std::string()) == "H2") {
        found = true;
        CHECK_EQ(o["up_mbps"].get<int>(), 100);
        CHECK_EQ(o["down_mbps"].get<int>(), 200);
      }
    }
    CHECK(found);
  }

  CHECK(!subconv::parse_clash_yaml("proxies: []", "t").has_value());
  CHECK(!subconv::parse_clash_yaml("这不是: [合法的: yaml", "t").has_value());

  // JSON 版 Clash 配置：YAML 是 JSON 的超集，mihomo 能吃，我们也要能在嗅探后直接当输入源
  const std::string json_clash = R"({"mixed-port":7890,"proxies":[
    {"name":"JSON-VL","type":"vless","server":"j.example.com","port":443,
     "uuid":"b831381d-6324-4d53-ad4f-8cda48b30811","tls":true,"network":"ws",
     "ws-opts":{"path":"/jw","headers":{"Host":"j.example.com"}}}]})";
  {
    auto json_sub = subconv::fetch::parse_content(json_clash, "粘贴内容");
    CHECK(json_sub.has_value());
    if (json_sub) {
      CHECK_EQ(json_sub->nodes.size(), static_cast<std::size_t>(1));
      if (!json_sub->nodes.empty()) {
        const auto& n = json_sub->nodes[0];
        CHECK_EQ(n.name, std::string("JSON-VL"));
        CHECK(n.protocol == subconv::Protocol::Vless);
        CHECK(n.network == subconv::Network::Ws);
        CHECK_EQ(n.ws.path, std::string("/jw"));
        CHECK_EQ(n.ws.host, std::string("j.example.com"));
        CHECK(n.tls.enabled);
      }
    }
  }
  // sing-box 的 JSON 仍然给「JSON 配置暂不支持」这类针对性报错
  {
    auto cfg = subconv::fetch::parse_content(
        R"({"log":{"level":"info"},"outbounds":[{"type":"direct","tag":"direct"}]})", "t");
    CHECK(!cfg.has_value());
    if (!cfg) CHECK(cfg.error().message.find("JSON 配置") != std::string::npos);
  }

  // yaml-cpp 的行号要翻译成人话：报出出问题的原始行，并提示缩进
  // （这就是「粘贴时整段丢了缩进」的真实死法：第二个节点开始报 end of map not found）
  {
    auto broken = subconv::parse_clash_yaml(
        "proxies:\n- name: a\ntype: ss\nserver: h\nport: 8388\ncipher: aes-256-gcm\n"
        "password: pw\n- name: b\ntype: ss\nserver: h2\nport: 8389\ncipher: aes-256-gcm\n"
        "password: pw\n",
        "t");
    CHECK(!broken.has_value());
    if (!broken) {
      const std::string message = broken.error().message;
      CHECK(message.find("Clash YAML 解析失败") != std::string::npos);
      CHECK(message.find("第 8 行: - name: b") != std::string::npos);
      CHECK(message.find("缩进") != std::string::npos);
    }
  }
}

// ---------------------------------------------------------------------------
// HTTP 服务：请求映射与转换核心（都是纯函数，不依赖 socket）
// ---------------------------------------------------------------------------
void test_server() {
  section("HTTP 服务 / 请求映射");

  using subconv::server::ConvertRequest;
  using subconv::server::ServerOptions;

  // 下载文件名
  CHECK_EQ(subconv::server::default_filename("clash"), std::string("subconv-clash.yaml"));
  CHECK_EQ(subconv::server::default_filename("clash.meta"), std::string("subconv-clash.yaml"));
  CHECK_EQ(subconv::server::default_filename("xray"), std::string("subconv-xray.json"));
  CHECK_EQ(subconv::server::default_filename("sing-box"), std::string("subconv-singbox.json"));

  // 路径 → 目标
  CHECK_EQ(subconv::server::target_from_path("/clash"), std::string("clash"));
  CHECK_EQ(subconv::server::target_from_path("/sing-box/"), std::string("singbox"));
  CHECK_EQ(subconv::server::target_from_path("/sub"), std::string());
  CHECK_EQ(subconv::server::target_from_path("/"), std::string());
  CHECK_EQ(subconv::server::target_from_path("/nope"), std::string());

  const std::string sample =
      "ss://YWVzLTI1Ni1nY206c3NwYXNz@ss.example.com:8443#SS-Node\n"
      "trojan://tjpw@tj.example.com:443?sni=tj.example.com#TROJAN\n";

  // 粘贴内容 → Clash
  {
    ConvertRequest req;
    req.content = sample;
    auto result = subconv::server::convert(req);
    CHECK(result.has_value());
    if (result) {
      CHECK_EQ(result->nodes, static_cast<std::size_t>(2));
      CHECK_EQ(result->filename, std::string("subconv-clash.yaml"));
      CHECK(result->config.find("proxies:") != std::string::npos);
      CHECK(result->config.find("proxy-groups:") != std::string::npos);
    }
  }

  // --no-emoji 语义：分组名不带 emoji
  {
    ConvertRequest req;
    req.content = sample;
    req.emit.emoji = false;
    auto result = subconv::server::convert(req);
    CHECK(result.has_value());
    if (result) {
      CHECK(result->config.find("节点选择") != std::string::npos);
      CHECK(result->config.find("🚀") == std::string::npos);
    }
  }

  // 目标切换 + 自定义文件名
  {
    ConvertRequest req;
    req.content = sample;
    req.emit.target = "xray";
    req.filename = "我的节点.json";
    auto result = subconv::server::convert(req);
    CHECK(result.has_value());
    if (result) {
      CHECK_EQ(result->filename, std::string("我的节点.json"));
      const Json parsed = Json::parse(result->config, nullptr, false);
      CHECK(!parsed.is_discarded());
      CHECK(parsed.contains("outbounds"));
    }
  }

  // 没有任何输入
  {
    ConvertRequest req;
    auto result = subconv::server::convert(req);
    CHECK(!result.has_value());
    CHECK(result.error().message.find("没有输入") != std::string::npos);
  }

  // 未实现的目标要报错（surge 仍是规划中）
  {
    ConvertRequest req;
    req.content = sample;
    req.emit.target = "surge";
    CHECK(!subconv::server::convert(req).has_value());
  }

  // parse_content 分派：HTML 错误页给针对性提示，而不是一堆 YAML 语法错
  {
    auto sub = subconv::fetch::parse_content("<html><body>404</body></html>", "t");
    CHECK(!sub.has_value());
    CHECK(sub.error().message.find("网页") != std::string::npos);
  }

  // ---- /sub 查询串映射 ----
  ServerOptions defaults;
  defaults.load.http.user_agent = "cli-ua";

  {
    auto req = subconv::server::request_from_query(
        "target=xray&url=https%3A%2F%2Fa.example%2Fsub&url=b.example%2Fsub"
        "&emoji=false&sort=true&timeout=7",
        defaults);
    CHECK(req.has_value());
    if (req) {
      CHECK_EQ(req->emit.target, std::string("xray"));
      CHECK_EQ(req->sources.size(), static_cast<std::size_t>(2));
      CHECK_EQ(req->sources[0], std::string("https://a.example/sub"));
      CHECK_EQ(req->sources[1], std::string("b.example/sub"));
      CHECK(!req->emit.emoji);
      CHECK(req->emit.sort);
      CHECK_EQ(req->load.http.timeout_seconds, 7L);
      CHECK_EQ(req->load.http.user_agent, std::string("cli-ua"));  // 未给则继承服务端默认
    }
  }

  // 一个 url 参数里用 | 分隔多个链接
  {
    auto req = subconv::server::request_from_query("url=a%7Cb%7Cc", defaults);
    CHECK(req.has_value());
    if (req) {
      CHECK_EQ(req->sources.size(), static_cast<std::size_t>(3));
      CHECK_EQ(req->emit.target, std::string("clash"));  // 回落到默认目标
    }
  }

  // content 内联正文
  {
    auto req = subconv::server::request_from_query("content=ss%3A%2F%2Fx%40h%3A1%23n", defaults);
    CHECK(req.has_value());
    if (req) CHECK_EQ(req->content, std::string("ss://x@h:1#n"));
  }

  // ---- 规则集参数 ----
  {
    // 不给 → 保留默认（local + cn）
    auto req = subconv::server::request_from_query("url=a.example/sub", defaults);
    CHECK(req.has_value());
    if (req) CHECK_EQ(req->emit.rule_sets, subconv::default_rule_sets());
  }
  {
    auto req = subconv::server::request_from_query("rulesets=ir,cloudflare,ads", defaults);
    CHECK(req.has_value());
    if (req) {
      CHECK_EQ(req->emit.rule_sets.size(), static_cast<std::size_t>(3));
      CHECK_EQ(req->emit.rule_sets[0], std::string("ir"));
      CHECK_EQ(req->emit.rule_sets[2], std::string("ads"));
    }
  }
  {
    // 给了空串 → 一个规则集都不要（只留 MATCH 兜底），不是「回落到默认」
    auto req = subconv::server::request_from_query("rulesets=", defaults);
    CHECK(req.has_value());
    if (req) CHECK(req->emit.rule_sets.empty());
  }

  // ---- POST /api/convert JSON 映射 ----
  {
    const std::string body = R"({
      "target": "singbox",
      "sources": ["https://a.example/sub", "https://b.example/sub|https://c.example/sub"],
      "options": {"emoji": false, "tfo": true},
      "fetch": {"proxy": "socks5://127.0.0.1:10808", "retries": 5,
                "headers": {"X-Token": "abc"}}
    })";
    auto req = subconv::server::request_from_json(body, defaults);
    CHECK(req.has_value());
    if (req) {
      CHECK_EQ(req->emit.target, std::string("singbox"));
      CHECK_EQ(req->sources.size(), static_cast<std::size_t>(3));
      CHECK(!req->emit.emoji);
      CHECK(req->emit.tfo);
      CHECK_EQ(req->load.http.proxy, std::string("socks5://127.0.0.1:10808"));
      CHECK_EQ(req->load.http.retries, 5);
      CHECK_EQ(req->load.http.headers["X-Token"], std::string("abc"));
    }
  }

  // 非法 JSON 要明确报错，而不是崩溃或静默
  {
    CHECK(!subconv::server::request_from_json("不是 json", defaults).has_value());
    CHECK(!subconv::server::request_from_json("[1,2,3]", defaults).has_value());
  }

  // JSON 里的规则集：数组与逗号字符串都要认；显式空数组 = 一个都不要
  {
    auto req = subconv::server::request_from_json(
        R"({"content":"x","options":{"rulesets":["ads","ir"]}})", defaults);
    CHECK(req.has_value());
    if (req) {
      CHECK_EQ(req->emit.rule_sets.size(), static_cast<std::size_t>(2));
      CHECK_EQ(req->emit.rule_sets[0], std::string("ads"));
      CHECK_EQ(req->emit.rule_sets[1], std::string("ir"));
    }
  }
  {
    auto req = subconv::server::request_from_json(
        R"({"content":"x","options":{"rulesets":"cn, ir ,"}})", defaults);
    CHECK(req.has_value());
    if (req) {
      CHECK_EQ(req->emit.rule_sets.size(), static_cast<std::size_t>(2));
      CHECK_EQ(req->emit.rule_sets[0], std::string("cn"));
      CHECK_EQ(req->emit.rule_sets[1], std::string("ir"));
    }
  }
  {
    auto req = subconv::server::request_from_json(
        R"({"content":"x","options":{"rulesets":[]}})", defaults);
    CHECK(req.has_value());
    if (req) CHECK(req->emit.rule_sets.empty());
  }
}

// ---------------------------------------------------------------------------
// CA 证书包：交给 libcurl 的 bundle 绝不能是空文件（MSYS2 上很容易踩到）
// ---------------------------------------------------------------------------
void test_ca_bundle() {
  section("libcurl / CA bundle");

  const std::string bundle = subconv::fetch::resolved_ca_bundle();
  if (!bundle.empty()) {
    std::ifstream probe(bundle, std::ios::binary | std::ios::ate);
    CHECK(probe.good());
    CHECK(probe.tellg() >= 4096);  // 0 字节 / 残缺的 bundle 一律不许下发
  }
}

// ---------------------------------------------------------------------------
// 控制台输出编码：人读文本按控制台代码页编码，避免 GBK 控制台显示成乱码
// ---------------------------------------------------------------------------
void test_console_encoding() {
  section("控制台输出编码");

  const std::string text = "订阅链接 · 已实现";
  CHECK_EQ(subconv::console::to_code_page(text, 0), text);      // 无控制台信息 → 原样
  CHECK_EQ(subconv::console::to_code_page(text, 65001), text);  // 已是 UTF-8 → 原样
  CHECK_EQ(subconv::console::to_code_page("", 936), std::string());

#ifdef _WIN32
  // GBK(936) 下每个汉字 2 字节；CP936 在现代 Windows 上都可用，
  // 万一没有（函数会原样返回 UTF-8）就跳过这组断言。
  const std::string gbk = subconv::console::to_code_page("订阅", 936);
  if (gbk.size() == 4) {
    CHECK_EQ(static_cast<unsigned char>(gbk[0]), 0xB6u);  // 订
    CHECK_EQ(static_cast<unsigned char>(gbk[1]), 0xA9u);
    CHECK_EQ(static_cast<unsigned char>(gbk[2]), 0xD4u);  // 阅
    CHECK_EQ(static_cast<unsigned char>(gbk[3]), 0xC4u);
  }
  // ASCII 在任何代码页下都不变
  CHECK_EQ(subconv::console::to_code_page("GET /api/version", 936),
           std::string("GET /api/version"));
#endif

  // 代码页决策：默认跟随控制台，环境变量可强制覆盖
  CHECK_EQ(subconv::console::effective_code_page(936, ""), 936u);
  CHECK_EQ(subconv::console::effective_code_page(0, ""), 0u);
  CHECK_EQ(subconv::console::effective_code_page(936, "auto"), 936u);
  CHECK_EQ(subconv::console::effective_code_page(936, "utf-8"), 65001u);
  CHECK_EQ(subconv::console::effective_code_page(936, "UTF8"), 65001u);
  CHECK_EQ(subconv::console::effective_code_page(65001, "936"), 936u);
  CHECK_EQ(subconv::console::effective_code_page(65001, "gbk"), 936u);
  CHECK_EQ(subconv::console::effective_code_page(65001, "cp936"), 936u);
  CHECK_EQ(subconv::console::effective_code_page(936, "不是代码页"), 936u);  // 认不出就退回默认
  CHECK_EQ(subconv::console::effective_code_page(936, "99999"), 936u);      // 越界同样退回
}

// ---------------------------------------------------------------------------
// 分享链接输出（v2rayNG 等）：round-trip —— 生成的链接必须能被自己原样解析回来
// ---------------------------------------------------------------------------
void test_share_links() {
  section("分享链接 / v2rayNG");

  // 目标名与别名
  CHECK_EQ(subconv::normalize_target("v2rayng"), std::string("base64"));
  CHECK_EQ(subconv::normalize_target("v2rayNG"), std::string("base64"));
  CHECK_EQ(subconv::normalize_target("v2rayng-sub"), std::string("base64"));
  CHECK_EQ(subconv::normalize_target("v2ray-sub"), std::string("base64"));
  CHECK_EQ(subconv::normalize_target("sssub"), std::string("base64"));
  CHECK_EQ(subconv::normalize_target("links"), std::string("links"));
  CHECK_EQ(subconv::normalize_target("sharelinks"), std::string("links"));
  CHECK_EQ(subconv::normalize_target("v2rayng-links"), std::string("links"));

  // base64 从「规划中」转为「已实现」
  const auto implemented = subconv::implemented_targets();
  const auto planned = subconv::planned_targets();
  CHECK(std::find(implemented.begin(), implemented.end(), "links") != implemented.end());
  CHECK(std::find(implemented.begin(), implemented.end(), "base64") != implemented.end());
  CHECK(std::find(planned.begin(), planned.end(), "base64") == planned.end());

  // 8 个可表示 + 5 个 v2rayNG 不支持的协议
  const std::string sample =
      "ss://YWVzLTI1Ni1nY206c3NwYXNz@ss.example.com:8443#SS-Node\n"
      "ss://Y2hhY2hhMjAtaWV0Zi1wb2x5MTMwNTpwdzI=@ss2.example.com:443/?plugin=obfs-local%3Bobfs%3Dhttp%3Bobfs-host%3Dwww.bing.com#SS-OBFS\n"
      "vmess://eyJ2IjoiMiIsInBzIjoiVk1lc3MtVENQIiwiYWRkIjoidm0zLmV4YW1wbGUuY29tIiwicG9ydCI6ODA4MCwiaWQiOiJiODMxMzgxZC02MzI0LTRkNTMtYWQ0Zi04Y2RhNDhiMzA4MTEiLCJhaWQiOjY0LCJzY3kiOiJhdXRvIiwibmV0IjoidGNwIiwidHlwZSI6Im5vbmUiLCJ0bHMiOiIifQ==\n"
      "vless://b831381d-6324-4d53-ad4f-8cda48b30811@vl.example.com:443?encryption=none&security=tls&sni=vl.example.com&type=ws&host=vl.example.com&path=%2Fvlws&fp=chrome&alpn=h2%2Chttp%2F1.1#VLESS-WS-TLS\n"
      "vless://b831381d-6324-4d53-ad4f-8cda48b30811@vl2.example.com:443?encryption=none&security=reality&sni=www.microsoft.com&fp=chrome&pbk=YQfPqk3nJP8vT1sBcDeFgHiJkLmNoPqRsTuVwXyZ012&sid=0123abcd&type=tcp&flow=xtls-rprx-vision#VLESS-REALITY\n"
      "trojan://tjpw@tj.example.com:443?sni=tj.example.com&type=ws&path=%2Ftjws&host=tj.example.com#TROJAN-WS\n"
      "hysteria2://hy2pw@hy2.example.com:443?sni=hy2.example.com&insecure=1&obfs=salamander&obfs-password=obfspw&up=100&down=200#HYSTERIA2\n"
      "socks5://user:pa%40ss@127.0.0.1:1080#Local-SOCKS\n"
      "https://1.2.3.4:443#TLS-HTTP-Proxy\n"
      "ssr://c3NyLmV4YW1wbGUuY29tOjgzODg6YXV0aF9hZXMxMjhfbWQ1OmFlcy0yNTYtY2ZiOmh0dHBfc2ltcGxlOmMzTnljR0Z6Y3cvP29iZnNwYXJhbT1kM2QzTG1KcGJtY3VZMjl0JnByb3RvcGFyYW09TVRJek5EVTZZV0pqJnJlbWFya3M9VTFOU0xVNXZaR1UmZ3JvdXA9ZEdWemRDMW5jbTkxY0E#SSR-Node\n"
      "snell://snellpsk@snell.example.com:443?version=4&obfs=http&obfs-host=www.bing.com#SNELL\n"
      "tuic://b831381d-6324-4d53-ad4f-8cda48b30811:tuicpw@tuic.example.com:443?congestion_control=bbr&alpn=h3&sni=tuic.example.com&udp_relay_mode=native&allow_insecure=1#TUIC\n"
      "hysteria://hy.example.com:443?auth=hypw&peer=hy.example.com&insecure=1&upmbps=100&downmbps=200&alpn=h3&protocol=udp#HYSTERIA1\n";

  auto sub = subconv::parse_subscription(sample, "test");
  CHECK(sub.has_value());
  if (!sub) return;
  CHECK_EQ(sub->nodes.size(), static_cast<std::size_t>(13));

  subconv::EmitOptions opts;
  opts.target = "links";
  opts.emoji = false;

  std::vector<std::string> warnings;
  auto links = subconv::emit_config(sub->nodes, opts, &warnings);
  CHECK(links.has_value());
  if (!links) return;

  // 每个被跳过的协议各一条汇总告警
  const std::string joined = join(warnings, " | ");
  CHECK_EQ(warnings.size(), static_cast<std::size_t>(5));
  CHECK(joined.find("http") != std::string::npos);
  CHECK(joined.find("ssr") != std::string::npos);
  CHECK(joined.find("hysteria") != std::string::npos);
  CHECK(joined.find("tuic") != std::string::npos);
  CHECK(joined.find("snell") != std::string::npos);

  // 逐行解析回来
  subconv::NodeList back;
  for (const auto& line : split(*links, '\n')) {
    if (trim(line).empty()) continue;
    auto node = subconv::parse_node(line);
    CHECK(node.has_value());
    if (node) back.push_back(*node);
  }
  CHECK_EQ(back.size(), static_cast<std::size_t>(8));

  auto find_by_name = [&back](const std::string& name) -> const subconv::ProxyNode* {
    for (const auto& node : back) {
      if (node.name == name) return &node;
    }
    return nullptr;
  };

  // Shadowsocks：口令与插件都要原样回来
  if (const auto* ss = find_by_name("SS-Node")) {
    CHECK(ss->protocol == subconv::Protocol::Shadowsocks);
    CHECK_EQ(ss->cipher, std::string("aes-256-gcm"));
    CHECK_EQ(ss->password, std::string("sspass"));
    CHECK(!ss->plugin.present);
  } else {
    CHECK(false);
  }
  if (const auto* ss = find_by_name("SS-OBFS")) {
    CHECK_EQ(ss->cipher, std::string("chacha20-ietf-poly1305"));
    CHECK(ss->plugin.present);
    CHECK_EQ(ss->plugin.obfs_mode, std::string("http"));
    CHECK_EQ(ss->plugin.obfs_host, std::string("www.bing.com"));
  } else {
    CHECK(false);
  }

  // VMess：JSON 里的 alterId / cipher 不能丢
  if (const auto* vm = find_by_name("VMess-TCP")) {
    CHECK(vm->protocol == subconv::Protocol::Vmess);
    CHECK_EQ(vm->uuid, std::string("b831381d-6324-4d53-ad4f-8cda48b30811"));
    CHECK_EQ(vm->alter_id, 64);
    CHECK_EQ(vm->cipher, std::string("auto"));
    CHECK(!vm->tls.enabled);
    CHECK_EQ(vm->port, static_cast<uint16_t>(8080));
  } else {
    CHECK(false);
  }

  // VLESS + TLS + WS
  if (const auto* vl = find_by_name("VLESS-WS-TLS")) {
    CHECK(vl->protocol == subconv::Protocol::Vless);
    CHECK(vl->tls.enabled);
    CHECK(!vl->tls.reality);
    CHECK_EQ(vl->tls.sni, std::string("vl.example.com"));
    CHECK(vl->network == subconv::Network::Ws);
    CHECK_EQ(vl->ws.path, std::string("/vlws"));
    CHECK_EQ(vl->ws.host, std::string("vl.example.com"));
    CHECK_EQ(vl->tls.client_fingerprint, std::string("chrome"));
    CHECK_EQ(vl->tls.alpn.size(), static_cast<std::size_t>(2));
  } else {
    CHECK(false);
  }

  // VLESS + REALITY
  if (const auto* vl = find_by_name("VLESS-REALITY")) {
    CHECK(vl->tls.enabled);
    CHECK(vl->tls.reality);
    CHECK_EQ(vl->tls.reality_public_key,
             std::string("YQfPqk3nJP8vT1sBcDeFgHiJkLmNoPqRsTuVwXyZ012"));
    CHECK_EQ(vl->tls.reality_short_id, std::string("0123abcd"));
    CHECK_EQ(vl->flow, std::string("xtls-rprx-vision"));
  } else {
    CHECK(false);
  }

  // Trojan + WS
  if (const auto* tj = find_by_name("TROJAN-WS")) {
    CHECK(tj->protocol == subconv::Protocol::Trojan);
    CHECK_EQ(tj->password, std::string("tjpw"));
    CHECK_EQ(tj->tls.sni, std::string("tj.example.com"));
    CHECK_EQ(tj->ws.path, std::string("/tjws"));
  } else {
    CHECK(false);
  }

  // Hysteria2：obfs / 带宽 / insecure
  if (const auto* hy = find_by_name("HYSTERIA2")) {
    CHECK(hy->protocol == subconv::Protocol::Hysteria2);
    CHECK_EQ(hy->password, std::string("hy2pw"));
    CHECK_EQ(hy->obfs, std::string("salamander"));
    CHECK_EQ(hy->obfs_password, std::string("obfspw"));
    CHECK(hy->tls.insecure);
    CHECK_EQ(hy->up, std::string("100"));
    CHECK_EQ(hy->down, std::string("200"));
  } else {
    CHECK(false);
  }

  // SOCKS：userinfo 是 base64(user:password)，解析器要能还原
  if (const auto* sk = find_by_name("Local-SOCKS")) {
    CHECK(sk->protocol == subconv::Protocol::Socks5);
    CHECK_EQ(sk->username, std::string("user"));
    CHECK_EQ(sk->password, std::string("pa@ss"));
  } else {
    CHECK(false);
  }

  // base64 目标 = 同一份链接列表的 base64
  subconv::EmitOptions b64_opts = opts;
  b64_opts.target = "base64";
  auto subscription = subconv::emit_config(sub->nodes, b64_opts);
  CHECK(subscription.has_value());
  if (subscription) {
    auto decoded = base64_decode(*subscription);
    CHECK(decoded.has_value());
    if (decoded) CHECK_EQ(*decoded, *links);
  }

  // ---- 线上格式契约（对齐 v2rayNG 的 Fmt 实现）----
  {
    auto line_with_prefix = [&links](std::string_view prefix) -> std::string {
      for (const auto& line : split(*links, '\n')) {
        if (line.rfind(prefix, 0) == 0) return line;
      }
      return {};
    };

    // SS：SIP002，userinfo 用 URL-safe 无 padding base64（标准 base64 的 + / = 会破坏 URI 解析）
    const std::string ss_userinfo = base64_encode_url("aes-256-gcm:sspass");
    CHECK(ss_userinfo.find('+') == std::string::npos);
    CHECK(ss_userinfo.find('/') == std::string::npos);
    CHECK(ss_userinfo.find('=') == std::string::npos);
    const std::string ss_line = "ss://" + ss_userinfo + "@ss.example.com:8443#SS-Node";
    auto ss_back = subconv::parse_node(ss_line);
    CHECK(ss_back.has_value());
    if (ss_back) CHECK_EQ(ss_back->password, std::string("sspass"));

    // vmess：base64(JSON)，18 个键、全字符串、顺序固定（v2rayNG 的 VmessQRCode 契约）
    const std::string vmess_line = line_with_prefix("vmess://");
    CHECK(!vmess_line.empty());
    if (!vmess_line.empty()) {
      auto json_text = base64_decode(std::string_view(vmess_line).substr(8));
      CHECK(json_text.has_value());
      if (json_text) {
        const Json payload = Json::parse(*json_text, nullptr, false);
        CHECK(!payload.is_discarded());
        const std::vector<std::string> expected_keys = {
            "v",    "ps",  "add", "port", "id",   "aid",      "scy", "net", "type",
            "host", "path", "tls", "sni",  "alpn", "fp",      "insecure", "vcn", "pcs"};
        CHECK_EQ(payload.size(), expected_keys.size());
        std::size_t index = 0;
        for (auto it = payload.begin(); it != payload.end() && index < expected_keys.size();
             ++it, ++index) {
          CHECK_EQ(it.key(), expected_keys[index]);  // ordered_json 保留插入顺序
          CHECK(it.value().is_string());
        }
        CHECK_EQ(index, expected_keys.size());
      }
    }

    // hysteria2：必须带 type=hysteria，否则 v2rayNG 导入后会把它当成 tcp 出站
    const std::string hy2_line = line_with_prefix("hysteria2://");
    CHECK(!hy2_line.empty());
    CHECK(hy2_line.find("type=hysteria") != std::string::npos);
    CHECK_EQ(hy2_line.find('#'), hy2_line.rfind('#'));  // 名称里没有多余 '#'
  }

  // 全部节点都不支持时要明确报错，而不是返回空串
  {
    const std::string only_http = "https://1.2.3.4:443#Only-HTTP\n";
    auto http_sub = subconv::parse_subscription(only_http, "test");
    CHECK(http_sub.has_value());
    if (http_sub) {
      std::vector<std::string> http_warnings;
      auto out = subconv::emit_config(http_sub->nodes, opts, &http_warnings);
      CHECK(!out.has_value());
      CHECK(!http_warnings.empty());
    }
  }
}

// ---------------------------------------------------------------------------
// v2rayn:// 分享项：能承载 http 等没有标准链接的协议（对齐 V2rayNShareItem）
// ---------------------------------------------------------------------------
void test_v2rayn_share() {
  section("v2rayNG 完整格式 (v2rayn://)");

  CHECK_EQ(subconv::normalize_target("v2rayn"), std::string("v2rayn"));
  CHECK_EQ(subconv::normalize_target("v2rayn-share"), std::string("v2rayn"));
  const auto implemented = subconv::implemented_targets();
  CHECK(std::find(implemented.begin(), implemented.end(), "v2rayn") != implemented.end());

  const std::string sample =
      "ss://YWVzLTI1Ni1nY206c3NwYXNz@ss.example.com:8443#SS-Node\n"
      "vmess://eyJ2IjoiMiIsInBzIjoiVk1lc3MtVENQIiwiYWRkIjoidm0zLmV4YW1wbGUuY29tIiwicG9ydCI6ODA4MCwiaWQiOiJiODMxMzgxZC02MzI0LTRkNTMtYWQ0Zi04Y2RhNDhiMzA4MTEiLCJhaWQiOjY0LCJzY3kiOiJhdXRvIiwibmV0IjoidGNwIiwidHlwZSI6Im5vbmUiLCJ0bHMiOiIifQ==\n"
      "vless://b831381d-6324-4d53-ad4f-8cda48b30811@vl2.example.com:443?encryption=none&security=reality&sni=www.microsoft.com&fp=chrome&pbk=YQfPqk3nJP8vT1sBcDeFgHiJkLmNoPqRsTuVwXyZ012&sid=0123abcd&type=tcp&flow=xtls-rprx-vision#VLESS-REALITY\n"
      "hysteria2://hy2pw@hy2.example.com:443?sni=hy2.example.com&insecure=1&obfs=salamander&obfs-password=obfspw&up=100&down=200#HYSTERIA2\n"
      "socks5://user:pa%40ss@127.0.0.1:1080#Local-SOCKS\n"
      "https://1.2.3.4:443#TLS-HTTP-Proxy\n"
      "ssr://c3NyLmV4YW1wbGUuY29tOjgzODg6YXV0aF9hZXMxMjhfbWQ1OmFlcy0yNTYtY2ZiOmh0dHBfc2ltcGxlOmMzTnljR0Z6Y3cvP29iZnNwYXJhbT1kM2QzTG1KcGJtY3VZMjl0JnByb3RvcGFyYW09TVRJek5EVTZZV0pqJnJlbWFya3M9VTFOU0xVNXZaR1UmZ3JvdXA9ZEdWemRDMW5jbTkxY0E#SSR-Node\n"
      "snell://snellpsk@snell.example.com:443?version=4&obfs=http&obfs-host=www.bing.com#SNELL\n"
      "tuic://b831381d-6324-4d53-ad4f-8cda48b30811:tuicpw@tuic.example.com:443?congestion_control=bbr&alpn=h3&sni=tuic.example.com&udp_relay_mode=native&allow_insecure=1#TUIC\n"
      "hysteria://hy.example.com:443?auth=hypw&peer=hy.example.com&insecure=1&upmbps=100&downmbps=200&alpn=h3&protocol=udp#HYSTERIA1\n";

  auto sub = subconv::parse_subscription(sample, "test");
  CHECK(sub.has_value());
  if (!sub) return;
  CHECK_EQ(sub->nodes.size(), static_cast<std::size_t>(10));

  subconv::EmitOptions opts;
  opts.target = "v2rayn";
  opts.emoji = false;

  std::vector<std::string> warnings;
  auto out = subconv::emit_config(sub->nodes, opts, &warnings);
  CHECK(out.has_value());
  if (!out) return;

  // http 在这个格式下是可表达的 → 10 个里应该出来 6 个（含 http），跳过 4 个，
  // 另外多一条 http+TLS 的说明（v2rayNG 的 http 出站不写 streamSettings）
  CHECK_EQ(warnings.size(), static_cast<std::size_t>(5));
  const std::string joined = join(warnings, " | ");
  CHECK(joined.find("ssr") != std::string::npos);
  CHECK(joined.find("snell") != std::string::npos);
  CHECK(joined.find("tuic") != std::string::npos);
  CHECK(joined.find("hysteria") != std::string::npos);
  CHECK(joined.find("v2rayNG 的 http 出站不写 streamSettings") != std::string::npos);

  // 路径段 = EConfigType.ToString().ToLower()，v2rayN 的 InnerFmt 靠它取载荷
  auto segment_of = [](int config_type) -> const char* {
    switch (config_type) {
      case 1: return "vmess";
      case 3: return "shadowsocks";
      case 4: return "socks";
      case 5: return "vless";
      case 6: return "trojan";
      case 7: return "hysteria2";
      case 10: return "http";
      default: return nullptr;
    }
  };

  std::vector<Json> items;
  std::set<std::string> index_ids;
  for (const auto& line : split(*out, '\n')) {
    if (trim(line).empty()) continue;
    CHECK_EQ(line.substr(0, 9), std::string("v2rayn://"));
    const std::string rest = line.substr(9);
    const std::size_t slash = rest.find('/');
    // 必须带 <协议>/ 路径段：缺了它 v2rayN 取到空 AbsolutePath，整条丢弃
    CHECK(slash != std::string::npos);
    if (slash == std::string::npos) continue;
    const std::string payload = rest.substr(slash + 1);
    // 载荷必须是 URL-safe base64：标准 base64 的 '/' 会被 substringAfterLast('/') 截断
    CHECK(payload.find('/') == std::string::npos);
    CHECK(payload.find('+') == std::string::npos);
    auto decoded = base64_decode(payload);
    CHECK(decoded.has_value());
    if (!decoded) continue;
    const Json item = Json::parse(*decoded, nullptr, false);
    CHECK(!item.is_discarded());
    if (item.is_discarded()) continue;
    // v2rayN 的 InnerFmt.ResolveSingle 只接受 ConfigVersion == 4
    CHECK_EQ(item.value("ConfigVersion", 0), 4);
    const char* expected = segment_of(item.value("ConfigType", 0));
    CHECK(expected != nullptr);
    if (expected != nullptr) CHECK_EQ(rest.substr(0, slash), std::string(expected));
    index_ids.insert(item.value("IndexId", std::string()));
    items.push_back(item);
  }
  CHECK_EQ(items.size(), static_cast<std::size_t>(6));
  // IndexId 重复会被 v2rayNG 的 putIfAbsent 静默丢弃
  CHECK_EQ(index_ids.size(), items.size());

  auto find_type = [&items](int config_type) -> const Json* {
    for (const auto& item : items) {
      if (item.value("ConfigType", 0) == config_type) return &item;
    }
    return nullptr;
  };

  // http（10）：这是标准分享链接做不到的
  if (const Json* http = find_type(10)) {
    CHECK_EQ((*http)["Address"].get<std::string>(), std::string("1.2.3.4"));
    CHECK_EQ((*http)["Port"].get<int>(), 443);
    CHECK((*http)["Port"].is_number());  // schema 要求整数
    CHECK_EQ((*http)["StreamSecurity"].get<std::string>(), std::string("tls"));
    CHECK_EQ((*http)["AllowInsecure"].get<std::string>(), std::string("true"));
  } else {
    CHECK(false);
  }

  // vmess（1）：UUID 放 Password，alterId/cipher 放 ProtoExtraObj
  if (const Json* vm = find_type(1)) {
    CHECK_EQ((*vm)["Password"].get<std::string>(),
             std::string("b831381d-6324-4d53-ad4f-8cda48b30811"));
    CHECK_EQ((*vm)["ProtoExtraObj"]["AlterId"].get<int>(), 64);
    CHECK_EQ((*vm)["ProtoExtraObj"]["VmessSecurity"].get<std::string>(), std::string("auto"));
  } else {
    CHECK(false);
  }

  // ss（3）
  if (const Json* ss = find_type(3)) {
    CHECK_EQ((*ss)["Password"].get<std::string>(), std::string("sspass"));
    CHECK_EQ((*ss)["ProtoExtraObj"]["SsMethod"].get<std::string>(), std::string("aes-256-gcm"));
  } else {
    CHECK(false);
  }

  // vless（5）+ reality
  if (const Json* vl = find_type(5)) {
    CHECK_EQ((*vl)["StreamSecurity"].get<std::string>(), std::string("reality"));
    CHECK_EQ((*vl)["PublicKey"].get<std::string>(),
             std::string("YQfPqk3nJP8vT1sBcDeFgHiJkLmNoPqRsTuVwXyZ012"));
    CHECK_EQ((*vl)["ShortId"].get<std::string>(), std::string("0123abcd"));
    CHECK_EQ((*vl)["ProtoExtraObj"]["Flow"].get<std::string>(), std::string("xtls-rprx-vision"));
    CHECK_EQ((*vl)["ProtoExtraObj"]["VlessEncryption"].get<std::string>(), std::string("none"));
  } else {
    CHECK(false);
  }

  // hysteria2 取 v2rayN 的 EConfigType 编号 7
  // （v2rayNG 自己的枚举把 7 给了 WireGuard，但它的 V2rayNShareItem 导入按 v2rayN 的编号走）
  if (const Json* hy = find_type(7)) {
    CHECK_EQ((*hy)["Password"].get<std::string>(), std::string("hy2pw"));
    CHECK_EQ((*hy)["ProtoExtraObj"]["SalamanderPass"].get<std::string>(), std::string("obfspw"));
    CHECK_EQ((*hy)["ProtoExtraObj"]["UpMbps"].get<int>(), 100);
    CHECK_EQ((*hy)["ProtoExtraObj"]["DownMbps"].get<int>(), 200);
  } else {
    CHECK(false);
  }

  // socks（4）
  if (const Json* sk = find_type(4)) {
    CHECK_EQ((*sk)["Username"].get<std::string>(), std::string("user"));
    CHECK_EQ((*sk)["Password"].get<std::string>(), std::string("pa@ss"));
  } else {
    CHECK(false);
  }
}

// ---------------------------------------------------------------------------
// XHTTP（Xray 的 splithttp / mihomo 的 xhttp）
//
// 一元机场（smallstrawberry）2026 年的 Clash 订阅就是这个形态：vless + network: xhttp，
// 并用 download-settings 把「下载」指向另一台机器。三个内核对它的支持并不一致
// （mihomo 只接 vless、sing-box 根本没有），这里逐条钉住解析与三个目标的产出。
// ---------------------------------------------------------------------------
void test_xhttp() {
  section("XHTTP 传输");

  const std::string yaml = R"(
proxies:
  - name: XH
    type: vless
    server: up.example.com
    port: 443
    uuid: 9129efa3-af16-4c74-8de4-f1ae26e6105e
    udp: true
    tls: true
    servername: update.microsoft.com
    skip-cert-verify: true
    network: xhttp
    xhttp-opts:
      path: /path
      mode: stream-up
      download-settings:
        path: /path
        server: down.example.com
        port: 8443
        servername: update.microsoft.com
  - name: VX
    type: vmess
    server: vm.example.com
    port: 443
    uuid: 9129efa3-af16-4c74-8de4-f1ae26e6105e
    cipher: auto
    tls: true
    network: xhttp
    xhttp-opts:
      path: /vx
      mode: packet-up
)";
  auto sub = subconv::parse_clash_yaml(yaml, "test");
  CHECK(sub.has_value());
  if (!sub) return;
  CHECK_EQ(sub->nodes.size(), std::size_t{2});

  const subconv::ProxyNode& xh = sub->nodes[0];
  CHECK(xh.network == subconv::Network::Xhttp);
  CHECK_EQ(xh.xhttp.path, std::string("/path"));
  CHECK_EQ(xh.xhttp.mode, std::string("stream-up"));
  CHECK(xh.xhttp.download.present);
  CHECK_EQ(xh.xhttp.download.server, std::string("down.example.com"));
  CHECK(xh.xhttp.download.port.has_value());
  CHECK_EQ(*xh.xhttp.download.port, static_cast<uint16_t>(8443));
  CHECK_EQ(xh.xhttp.download.sni, std::string("update.microsoft.com"));
  CHECK_EQ(xh.xhttp.download.path, std::string("/path"));
  // 没写的字段必须是「未指定」——填了默认值就把内核的"沿用主节点"改成"覆盖"了
  CHECK(!xh.xhttp.download.tls.has_value());
  CHECK(!xh.xhttp.download.insecure.has_value());

  // --- clash（mihomo）---
  subconv::EmitOptions co;
  co.target = "clash";
  std::vector<std::string> cw;
  auto clash = subconv::emit_config(sub->nodes, co, &cw);
  CHECK(clash.has_value());
  if (clash) {
    CHECK(clash->find("network: xhttp") != std::string::npos);
    CHECK(clash->find("mode: stream-up") != std::string::npos);
    CHECK(clash->find("server: down.example.com") != std::string::npos);
    CHECK(clash->find("port: 8443") != std::string::npos);
    // mihomo 的 xhttp 只挂在 vless 出站上：vmess 那个要跳过并告警，而不是产出连不上的配置
    CHECK(clash->find("vm.example.com") == std::string::npos);
    CHECK_EQ(cw.size(), std::size_t{1});
  }

  // 原版 Clash 连 vless 都没有，xhttp 更不可能有
  {
    subconv::EmitOptions legacy;
    legacy.target = "clash";
    legacy.clash_legacy = true;
    auto out = subconv::emit_config(sub->nodes, legacy);
    CHECK(!out.has_value());
  }

  // --- xray ---
  subconv::EmitOptions xo;
  xo.target = "xray";
  std::vector<std::string> xw;
  auto xray = subconv::emit_config(sub->nodes, xo, &xw);
  CHECK(xray.has_value());
  if (xray) {
    const Json j = Json::parse(*xray, nullptr, false);
    CHECK(!j.is_discarded());
    const Json* vless_out = nullptr;
    bool has_vmess = false;
    for (const auto& o : j["outbounds"]) {
      const std::string tag = o.value("tag", std::string());
      if (tag == "XH") vless_out = &o;
      if (tag == "VX") has_vmess = true;
    }
    CHECK(vless_out != nullptr);
    if (vless_out != nullptr) {
      const Json& stream = (*vless_out)["streamSettings"];
      CHECK_EQ(stream["network"], std::string("xhttp"));
      CHECK_EQ(stream["xhttpSettings"]["path"], std::string("/path"));
      CHECK_EQ(stream["xhttpSettings"]["mode"], std::string("stream-up"));
      const Json& ds = stream["xhttpSettings"]["downloadSettings"];
      CHECK_EQ(ds["address"], std::string("down.example.com"));
      CHECK_EQ(ds["port"], 8443);
      CHECK_EQ(ds["network"], std::string("xhttp"));
      CHECK_EQ(ds["security"], std::string("tls"));
      CHECK_EQ(ds["xhttpSettings"]["path"], std::string("/path"));
      CHECK_EQ(ds["tlsSettings"]["serverName"], std::string("update.microsoft.com"));
      // 主节点 skip-cert-verify 为真：Xray 的 downloadSettings 是另起一份 StreamConfig、
      // 不会继承，必须显式补 verifyPeerCertByName，否则下载方向会严格校验证书
      CHECK(ds["tlsSettings"].contains("verifyPeerCertByName"));
    }
    // Xray 支持 vmess + xhttp，不能跟着 mihomo 一起裁掉
    CHECK(has_vmess);
    // 两个节点都是 skip-cert-verify 又没探测过指纹 → 一条汇总告警，提醒用 --probe-cert
    CHECK_EQ(xw.size(), std::size_t{1});
    if (!xw.empty()) CHECK(xw[0].find("probe-cert") != std::string::npos);
  }

  // --- sing-box：没有 xhttp，整份都留不下 ---
  subconv::EmitOptions so;
  so.target = "singbox";
  std::vector<std::string> sw;
  auto sb = subconv::emit_config(sub->nodes, so, &sw);
  CHECK(!sb.has_value());
  CHECK_EQ(sw.size(), std::size_t{2});
  if (!sw.empty()) CHECK(sw[0].find("xhttp") != std::string::npos);

  // --- 分享链接（v2rayNG / v2rayN 的 type=xhttp + extra=JSON）---
  subconv::EmitOptions lo;
  lo.target = "links";
  std::vector<std::string> lw;
  auto links = subconv::emit_config(sub->nodes, lo, &lw);
  CHECK(links.has_value());
  if (links) {
    CHECK(links->find("type=xhttp") != std::string::npos);
    CHECK(links->find("mode=stream-up") != std::string::npos);
    CHECK(links->find("extra=") != std::string::npos);

    // 往返：把我们生成的链接解析回来，download-settings 必须逐字段还原
    bool checked = false;
    for (const auto& line : split(*links, '\n')) {
      if (line.empty()) continue;
      auto node = subconv::parse_node(line);
      CHECK(node.has_value());
      if (!node || node->name != "XH") continue;
      checked = true;
      CHECK(node->network == subconv::Network::Xhttp);
      CHECK_EQ(node->xhttp.path, std::string("/path"));
      CHECK_EQ(node->xhttp.mode, std::string("stream-up"));
      CHECK(node->xhttp.download.present);
      CHECK_EQ(node->xhttp.download.server, std::string("down.example.com"));
      CHECK(node->xhttp.download.port.has_value());
      CHECK_EQ(*node->xhttp.download.port, static_cast<uint16_t>(8443));
      CHECK_EQ(node->xhttp.download.sni, std::string("update.microsoft.com"));
      // extra 里带着 tlsSettings → 解析回 tls=true / insecure=true，语义与"沿用主节点"等价
      CHECK(node->xhttp.download.tls.value_or(false));
      CHECK(node->xhttp.download.insecure.value_or(false));
      // 原始 extra 原样留在 node.extra 里，供 Xray 目标透传
      CHECK(node->extra.find("xhttpExtra") != node->extra.end());
    }
    CHECK(checked);

    // 纯 vless+xhttp 的 links 输出能被 v2rayn 目标复用（XhttpMode/XhttpExtra 两个字段）
    subconv::EmitOptions vo;
    vo.target = "v2rayn";
    auto v2 = subconv::emit_config(sub->nodes, vo, &lw);
    CHECK(v2.has_value());
    if (v2) {
      bool found = false;
      for (const auto& line : split(*v2, '\n')) {
        if (line.empty()) continue;
        const auto payload = base64_decode(line.substr(line.rfind('/') + 1));
        if (!payload) continue;
        if (payload->find("\"XhttpMode\":\"stream-up\"") != std::string::npos) found = true;
      }
      CHECK(found);
    }
  }

  // --- 证书指纹：Xray 25+ 移除 allowInsecure 后，唯一能放行「证书与 SNI 对不上」的参数 ---
  const std::string pin =
      "BE:3F:8A:0E:2A:17:A8:DF:FD:B6:23:67:EE:7B:90:5B:28:17:3C:47:78:E3:63:59:D1:07:2F:48:"
      "02:D8:B1:2F";
  {
    // 链接里的 `pcs=`（v2rayN / v2rayNG 的写法，也是 --probe-cert 写出来的东西）必须被解析
    auto node = subconv::parse_node(
        "vless://9129efa3-af16-4c74-8de4-f1ae26e6105e@pin.example.com:443"
        "?encryption=none&security=tls&sni=update.microsoft.com&type=xhttp&path=%2Fpath"
        "&pcs=BE%3A3F%3A8A%3A0E%3A2A%3A17%3AA8%3ADF%3AFD%3AB6%3A23%3A67%3AEE%3A7B%3A90%3A5B"
        "%3A28%3A17%3A3C%3A47%3A78%3AE3%3A63%3A59%3AD1%3A07%3A2F%3A48%3A02%3AD8%3AB1%3A2F"
        "&allowInsecure=1#PIN");
    CHECK(node.has_value());
    if (node) {
      CHECK_EQ(node->tls.pinned_cert_sha256, pin);
      subconv::EmitOptions xo2;
      xo2.target = "xray";
      std::vector<std::string> w2;
      auto out2 = subconv::emit_config({*node}, xo2, &w2);
      CHECK(out2.has_value());
      if (out2) {
        const Json j = Json::parse(*out2, nullptr, false);
        const Json& tls = j["outbounds"][0]["streamSettings"]["tlsSettings"];
        CHECK_EQ(tls["pinnedPeerCertSha256"], pin);
        // 有指纹就不该再写 vcn：Xray 侧 vcn 仍要求链可信 + 名字匹配，只会帮倒忙
        CHECK(!tls.contains("verifyPeerCertByName"));
      }
      CHECK(w2.empty());  // 有指纹 → 不再告警
    }
  }
  {
    // 没指纹时退回 vcn，并且必须告警告诉用户怎么修（这就是「客户端全是 -1」的根因）
    subconv::ProxyNode node;
    node.protocol = subconv::Protocol::Vless;
    node.name = "no-pin";
    node.server = "np.example.com";
    node.port = 443;
    node.uuid = "9129efa3-af16-4c74-8de4-f1ae26e6105e";
    node.tls.enabled = true;
    node.tls.insecure = true;
    node.tls.sni = "update.microsoft.com";
    subconv::EmitOptions xo3;
    xo3.target = "xray";
    std::vector<std::string> w3;
    auto out3 = subconv::emit_config({node}, xo3, &w3);
    CHECK(out3.has_value());
    if (out3) {
      const Json j = Json::parse(*out3, nullptr, false);
      CHECK_EQ(j["outbounds"][0]["streamSettings"]["tlsSettings"]["verifyPeerCertByName"],
               std::string("update.microsoft.com"));
    }
    CHECK_EQ(w3.size(), std::size_t{1});
    if (!w3.empty()) CHECK(w3[0].find("probe-cert") != std::string::npos);
  }
}

// VLESS Encryption（Xray 的 XTLS Vision Seed / ML-KEM 扩展）。
// 曾经的通病：vless:// 转 Clash 时这个参数被中间层丢掉 → 服务端解不开 VLESS 头，
// 既不回包也不断开，客户端只能等到拨号超时（「连上了但一直没数据」）。
void test_vless_encryption() {
  section("VLESS Encryption");
  const std::string uuid = "b831381d-6324-4d53-ad4f-8cda48b30811";
  // Xray 文档里的样例形态：最后一块是 `xray mlkem768` 的 Client 段
  const std::string key = "ptjHQxBQxTJ9MWr2cd5qWIflBSACHOevTauCQwa_71U";
  const std::string enc = "mlkem768x25519plus.native.0rtt." + key;

  // --- 结构解析：合法形态 ---
  {
    const subconv::VlessEncryption info = subconv::parse_vless_encryption(enc);
    CHECK(info.present);
    CHECK_EQ(info.raw, enc);
    CHECK_EQ(info.handshake, std::string("mlkem768x25519plus"));
    CHECK_EQ(info.appearance, std::string("native"));
    CHECK_EQ(info.rtt, std::string("0rtt"));
    CHECK_EQ(info.padding_blocks, std::size_t{0});
    CHECK_EQ(info.key, key);
    CHECK(info.problem.empty());
  }
  // 文档里带 padding 的完整写法：padding.delay.padding + 认证参数
  {
    const std::string padded =
        "mlkem768x25519plus.xorpub.1rtt.100-111-1111.75-0-111.50-0-3333." + key;
    const subconv::VlessEncryption info = subconv::parse_vless_encryption(padded);
    CHECK(info.present);
    CHECK_EQ(info.appearance, std::string("xorpub"));
    CHECK_EQ(info.rtt, std::string("1rtt"));
    CHECK_EQ(info.padding_blocks, std::size_t{3});
    CHECK_EQ(info.key, key);
    CHECK(info.problem.empty());
  }

  // --- 结构解析：none / 空 / 各种坏写法都要能识别出来 ---
  CHECK(!subconv::parse_vless_encryption("none").present);
  CHECK(!subconv::parse_vless_encryption("").present);
  CHECK(!subconv::parse_vless_encryption("  ").present);
  CHECK_EQ(subconv::normalize_vless_encryption("NONE"), std::string());
  CHECK_EQ(subconv::normalize_vless_encryption("  " + enc + " "), enc);
  // 未知握手方式
  {
    const subconv::VlessEncryption info =
        subconv::parse_vless_encryption("aes-128-gcm.native.0rtt." + key);
    CHECK(info.present);
    CHECK(!info.problem.empty());
  }
  // 缺认证参数（只有三块）
  CHECK(!subconv::parse_vless_encryption("mlkem768x25519plus.native.0rtt").problem.empty());
  // 末尾多了一个点 → 空块
  CHECK(!subconv::parse_vless_encryption(enc + ".").problem.empty());
  // padding 以 delay 结尾（块数为偶）不合法
  CHECK(!subconv::parse_vless_encryption(enc + ".50-0-3333.75-0-111").problem.empty());
  // 首个 padding 必须 100% 且最小长度 > 0
  CHECK(!subconv::parse_vless_encryption(enc + ".50-0-3333.75-0-111.100-0-1").problem.empty());
  // 概率超过 100
  CHECK(!subconv::parse_vless_encryption(enc + ".101-1-2").problem.empty());

  // --- 分享链接解析 ---
  auto node = subconv::parse_node(
      "vless://" + uuid +
      "@enc.example.com:443?encryption=" + enc +
      "&security=tls&sni=enc.example.com&type=ws&path=%2Fws&host=enc.example.com"
      "&flow=xtls-rprx-vision#ENC");
  CHECK(node.has_value());
  if (!node) return;
  CHECK_EQ(node->encryption, enc);
  CHECK(subconv::vless_encryption_enabled(*node));
  CHECK_EQ(subconv::vless_encryption_of(*node), enc);
  CHECK(subconv::vless_encryption_warning(*node).empty());  // 参数完整 → 不告警

  // encryption=none 不该在节点上留下任何东西
  auto plain = subconv::parse_node(
      "vless://" + uuid + "@p.example.com:443?encryption=none&security=tls&sni=p.example.com#P");
  CHECK(plain.has_value());
  if (plain) {
    CHECK(plain->encryption.empty());
    CHECK(!subconv::vless_encryption_enabled(*plain));
  }
  // flow=vision 但既不是 TCP+TLS、也没开 encryption → 必须告警（这种组合握手必失败）
  auto vision_ws = subconv::parse_node(
      "vless://" + uuid +
      "@b.example.com:443?security=tls&sni=b.example.com&type=ws&path=%2Fws"
      "&flow=xtls-rprx-vision#B");
  CHECK(vision_ws.has_value());
  if (vision_ws) CHECK(!subconv::vless_encryption_warning(*vision_ws).empty());
  // TCP + TLS + vision 是合法组合，不该告警
  auto vision_tcp = subconv::parse_node(
      "vless://" + uuid +
      "@t.example.com:443?security=tls&sni=t.example.com&type=tcp&flow=xtls-rprx-vision#T");
  CHECK(vision_tcp.has_value());
  if (vision_tcp) CHECK(subconv::vless_encryption_warning(*vision_tcp).empty());

  // --- Clash 目标：必须写出 encryption（这正是历史 bug） ---
  subconv::EmitOptions clash_opts;
  clash_opts.target = "clash";
  std::vector<std::string> clash_warnings;
  auto clash = subconv::emit_config({*node}, clash_opts, &clash_warnings);
  CHECK(clash.has_value());
  if (clash) {
    CHECK(clash->find("encryption: " + enc + "\n") != std::string::npos);
    CHECK(clash->find("flow: xtls-rprx-vision") != std::string::npos);
    // 参数完整时不该产生任何告警
    CHECK(clash_warnings.empty());
  }
  // 结构不对的 encryption 要告警，但值照样原样写进产物
  {
    subconv::ProxyNode bad = *node;
    bad.encryption = "mlkem768x25519plus.native.0rtt";  // 少了认证参数
    std::vector<std::string> warn;
    subconv::EmitOptions opts;
    opts.target = "clash";
    auto out = subconv::emit_config({bad}, opts, &warn);
    CHECK(out.has_value());
    if (out) CHECK(out->find("encryption: mlkem768x25519plus.native.0rtt\n") != std::string::npos);
    CHECK_EQ(warn.size(), std::size_t{1});
    if (!warn.empty()) CHECK(warn[0].find("encryption") != std::string::npos);
  }

  // --- Xray 目标：encryption 不能是写死的 none ---
  subconv::EmitOptions xray_opts;
  xray_opts.target = "xray";
  auto xray = subconv::emit_config({*node}, xray_opts);
  CHECK(xray.has_value());
  if (xray) {
    const Json j = Json::parse(*xray, nullptr, false);
    CHECK(!j.is_discarded());
    if (!j.is_discarded()) {
      bool found_vless = false;
      for (const auto& o : j["outbounds"]) {
        if (o["protocol"] != "vless") continue;
        found_vless = true;
        CHECK_EQ(o["settings"]["vnext"][0]["users"][0]["encryption"], enc);
        CHECK_EQ(o["settings"]["vnext"][0]["users"][0]["flow"], std::string("xtls-rprx-vision"));
      }
      CHECK(found_vless);
    }
  }
  // 没开加密时依旧是显式的 "none"（Xray 的这个字段不允许留空）
  if (plain) {
    auto xray_plain = subconv::emit_config({*plain}, xray_opts);
    CHECK(xray_plain.has_value());
    if (xray_plain) {
      const Json j = Json::parse(*xray_plain, nullptr, false);
      CHECK(!j.is_discarded());
      if (!j.is_discarded()) {
        CHECK_EQ(j["outbounds"][0]["settings"]["vnext"][0]["users"][0]["encryption"],
                 std::string("none"));
      }
    }
  }

  // --- sing-box 目标：它没有 VLESS Encryption，必须跳过并说明 ---
  {
    subconv::EmitOptions singbox_opts;
    singbox_opts.target = "singbox";
    std::vector<std::string> warn;
    auto singbox = subconv::emit_config({*node}, singbox_opts, &warn);
    CHECK(!singbox.has_value());  // 唯一节点被跳过 → 没有可输出的节点
    const std::string joined = join(warn, " | ");
    CHECK(joined.find("VLESS Encryption") != std::string::npos);
  }

  // --- 分享链接：原样回写，且能自己解析回来 ---
  {
    subconv::EmitOptions link_opts;
    link_opts.target = "links";
    auto links = subconv::emit_config({*node}, link_opts);
    CHECK(links.has_value());
    if (links) {
      CHECK(links->find("encryption=" + enc) != std::string::npos);
      auto back = subconv::parse_node(*links);
      CHECK(back.has_value());
      if (back) {
        CHECK_EQ(back->encryption, enc);
        CHECK_EQ(back->flow, std::string("xtls-rprx-vision"));
      }
    }
    // v2rayn 目标写在 ProtoExtraObj.VlessEncryption 里（v2rayN 的字段名）
    subconv::EmitOptions v2rayn_opts;
    v2rayn_opts.target = "v2rayn";
    auto v2rayn = subconv::emit_config({*node}, v2rayn_opts);
    CHECK(v2rayn.has_value());
    if (v2rayn) {
      // 形如 v2rayn://vless/<base64url(JSON)>：路径段不能省，所以先剥 scheme 再找 '/'
      const std::string line = trim(*v2rayn);
      CHECK_EQ(line.substr(0, 9), std::string("v2rayn://"));
      const std::string rest = line.substr(9);
      const std::size_t slash = rest.find('/');
      CHECK(slash != std::string::npos);
      if (slash != std::string::npos) {
        auto decoded = base64_decode(rest.substr(slash + 1));
        CHECK(decoded.has_value());
        if (decoded) {
          const Json item = Json::parse(*decoded, nullptr, false);
          CHECK(!item.is_discarded());
          if (!item.is_discarded()) {
            CHECK_EQ(item["ProtoExtraObj"]["VlessEncryption"], enc);
            CHECK_EQ(item["ProtoExtraObj"]["Flow"], std::string("xtls-rprx-vision"));
          }
        }
      }
    }
  }

  // --- Clash YAML 作为输入源：读回来不能丢（否则「YAML 进 → 分享链接出」会掉参数） ---
  if (clash) {
    auto round = subconv::parse_clash_yaml(*clash, "test");
    CHECK(round.has_value());
    if (round) {
      bool found = false;
      for (const auto& n2 : round->nodes) {
        if (n2.protocol != subconv::Protocol::Vless) continue;
        found = true;
        CHECK_EQ(n2.encryption, enc);
        CHECK_EQ(n2.flow, std::string("xtls-rprx-vision"));
      }
      CHECK(found);
    }
  }
}

}  // namespace

int main() {
  std::printf("subconv 单元测试\n");
  test_base64();
  test_percent_and_uri();
  test_ss();
  test_socks_http();
  test_yaml();
  test_subscription();
  test_clash_emit();
  test_rulesets();
  test_dns_and_name();
  test_advanced_protocols();
  test_emit_targets();
  test_sniff_and_userinfo();
  test_clash_yaml_input();
  test_server();
  test_ca_bundle();
  test_console_encoding();
  test_share_links();
  test_v2rayn_share();
  test_xhttp();
  test_vless_encryption();

  std::printf("\n%d 项断言，%d 项失败\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
