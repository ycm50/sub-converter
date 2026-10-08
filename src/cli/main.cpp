// subconv CLI
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "subconv/codec.hpp"
#include "subconv/console.hpp"
#include "subconv/convert.hpp"
#include "subconv/fetch.hpp"
#include "subconv/fsutil.hpp"
#include "subconv/server.hpp"

namespace {

#ifdef SUBCONV_VERSION
constexpr const char* kVersion = SUBCONV_VERSION;
#else
constexpr const char* kVersion = "dev";
#endif

void print_usage(std::FILE* out) {
  const std::string text = std::string("subconv ") + kVersion + R"( - 订阅转换工具

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
                           支持：分享链接列表（含 Base64 包裹）、Clash YAML、
                                 Xray JSON 配置（含面板 ?app=xray 的配置数组）

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
                           自定义规则集的 id 也写在这里
      --ruleset-json <JSON>   追加自定义规则集：自己列域名 / 关键字，可重复
                               {"id":{"policy":"REJECT","rules":[
                                  {"type":"DOMAIN-KEYWORD","value":"ads"},
                                  {"type":"DOMAIN-SUFFIX","value":"doubleclick.net"}]}}
                               rules 也接受简写字符串 "DOMAIN-KEYWORD,ads"；
                               policy 可以是 DIRECT / REJECT，或某个代理组名（走代理）
      --ruleset-file <文件>   从文件读自定义规则集（格式同上），可重复
      --dns <列表>         dns.nameserver：预设 id 或字面地址（IP / DoH URL），逗号分隔
                           见 --list-dns，默认 cloudflare,google；给空串则不写 nameserver
      --ipv6 / --no-ipv6   根节点与 dns 段的 ipv6，默认开
      --name <订阅名>      写进配置首行注释，并作为下载文件名（自动补扩展名）

链式代理:
      --chain <值>         前置代理链路（别名 --front-proxy），可重复，
                           按「最外侧 -> 最内侧」的顺序；
                           产物变成「本地 -> chain1 -> chain2 -> ... -> 每个节点」
                           （节点自己拨号到目标）
                           值有两种写法：
                             * 分享链接（ss:// vmess:// vless:// trojan:// socks5:// http:// ...）
                               —— 作为独立出站追加进配置
                             * 订阅里某个节点的名字（可写 @名字）—— 直接复用那个节点
      --chain-rear <值>    后置代理链路（别名 --rear-proxy），可重复，
                           按「最内侧 -> 最外侧」的顺序；
                           产物变成「本地 -> ... -> 每个节点 -> rear1 -> rear2 -> ... -> 目标」
                           链路字段的方向与"谁拨号到目标"是反的，所以流量落点会变成后置链路的
                           **末端**：末端出站的名字写成「节点 -> 后置」，节点自己不改名，
                           照旧直连（或接在前置链路后面）。效果就是从客户端里选
                           「节点 -> 后置」得到「本地 -> 节点 -> 后置 -> 目标」
                           —— 正是"用 A 当入口、用 B 当出口"。
                           每个节点都会克隆一份后置链路（rear1 必须经各自的节点出去），
                           所以 N 个节点配 m 跳会多出 N×m 个出站。值的写法与 --chain 相同。
                           两者都落在：xray 的 sockopt.dialerProxy / clash 的 dialer-proxy /
                           singbox 的 detour；links / base64 / v2rayn 没有该字段，会告警并忽略
                           注意：链路按 TCP 建隧道，凡是"经隧道被拨到"的那一跳（前置链路下的
                           节点、后置链路的每一跳）若是 hysteria2 / tuic / wireguard 或
                           quic / kcp 传输就链不通（会告警）

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
)";
  subconv::console::write(out, text);
}

std::string join_plain(const std::vector<std::string>& v) { return subconv::codec::join(v, ", "); }

std::string default_cache_dir() {
  return subconv::fs::join(subconv::fs::temp_directory(), "subconv-cache");
}

std::string human_bytes(int64_t bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[unit]);
  return buffer;
}

struct Options {
  std::vector<std::string> inputs;
  std::string output;
  subconv::EmitOptions emit;
  subconv::fetch::LoadOptions load;
  bool show_help = false;
  bool show_version = false;
  bool list_targets = false;
  bool list_rulesets = false;
  bool list_dns = false;
  bool verbose = false;
  bool serve = false;
  std::string listen = "127.0.0.1";
  int port = 25500;
  bool open_browser = false;
};

int parse_args(int argc, char** argv, Options& opt, std::string& error) {
  auto need = [&](int& i, const char* flag) -> const char* {
    if (i + 1 >= argc) {
      error = std::string("选项 ") + flag + " 缺少参数值";
      return nullptr;
    }
    return argv[++i];
  };
  auto need_long = [&](int& i, const char* flag, long& target_out) -> bool {
    const char* v = need(i, flag);
    if (v == nullptr) return false;
    try {
      target_out = std::stol(v);
    } catch (...) {
      error = std::string("选项 ") + flag + " 需要整数参数，收到: " + v;
      return false;
    }
    return true;
  };

  opt.load.cache_dir = default_cache_dir();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help" || arg == "help") {
      opt.show_help = true;
    } else if (arg == "-V" || arg == "--version" || arg == "version") {
      opt.show_version = true;
    } else if (arg == "--list-targets") {
      opt.list_targets = true;
    } else if (arg == "convert") {
      // 子命令，忽略
    } else if (arg == "serve") {
      opt.serve = true;
    } else if (arg == "--listen") {
      const char* v = need(i, "--listen");
      if (v == nullptr) return -1;
      opt.listen = v;
    } else if (arg == "--port") {
      long value = 0;
      if (!need_long(i, "--port", value)) return -1;
      if (value < 0 || value > 65535) {
        error = "选项 --port 需要在 0-65535 之间，收到: " + std::to_string(value);
        return -1;
      }
      opt.port = static_cast<int>(value);
    } else if (arg == "--open") {
      opt.open_browser = true;
    } else if (arg == "-i" || arg == "--input") {
      const char* v = need(i, "-i");
      if (v == nullptr) return -1;
      opt.inputs.emplace_back(v);
    } else if (arg == "-t" || arg == "--target") {
      const char* v = need(i, "-t");
      if (v == nullptr) return -1;
      opt.emit.target = v;
    } else if (arg == "-o" || arg == "--output") {
      const char* v = need(i, "-o");
      if (v == nullptr) return -1;
      opt.output = v;
    } else if (arg == "--no-emoji") {
      opt.emit.emoji = false;
    } else if (arg == "--emoji") {
      opt.emit.emoji = true;
    } else if (arg == "--no-udp") {
      opt.emit.udp = false;
    } else if (arg == "--udp") {
      opt.emit.udp = true;
    } else if (arg == "--no-dedup") {
      opt.emit.dedup = false;
    } else if (arg == "--tfo") {
      opt.emit.tfo = true;
    } else if (arg == "--sort") {
      opt.emit.sort = true;
    } else if (arg == "--clash-legacy") {
      opt.emit.clash_legacy = true;
    } else if (arg == "--no-rules") {
      opt.emit.include_rules = false;
    } else if (arg == "--rulesets") {
      const char* v = need(i, "--rulesets");
      if (v == nullptr) return -1;
      // 逗号（也接受空格）分隔的规则集 id；给空串表示不要任何规则集，只留 MATCH 兜底
      opt.emit.rule_sets.clear();
      for (const auto& part : subconv::codec::split(v, ',')) {
        for (const auto& piece : subconv::codec::split(part, ' ')) {
          const std::string id = subconv::codec::trim(piece);
          if (!id.empty()) opt.emit.rule_sets.push_back(id);
        }
      }
    } else if (arg == "--list-rulesets") {
      opt.list_rulesets = true;
    } else if (arg == "--ruleset-json" || arg == "--custom-rulesets") {
      const char* v = need(i, "--ruleset-json");
      if (v == nullptr) return -1;
      // 直接解析成结构化规则；解析失败立刻报错，不带着半份规则往下跑
      auto parsed = subconv::parse_custom_rule_sets(v);
      if (!parsed) {
        error = std::string("--ruleset-json 解析失败：") + parsed.error().message;
        return -1;
      }
      for (auto& set : *parsed) opt.emit.custom_rule_sets.push_back(std::move(set));
    } else if (arg == "--ruleset-file") {
      const char* v = need(i, "--ruleset-file");
      if (v == nullptr) return -1;
      std::ifstream in(v, std::ios::binary);
      if (!in) {
        error = std::string("无法打开规则集文件 ") + v;
        return -1;
      }
      std::ostringstream buffer;
      buffer << in.rdbuf();
      auto parsed = subconv::parse_custom_rule_sets(buffer.str());
      if (!parsed) {
        error = std::string("规则集文件 ") + v + " 解析失败：" + parsed.error().message;
        return -1;
      }
      for (auto& set : *parsed) opt.emit.custom_rule_sets.push_back(std::move(set));
    } else if (arg == "--dns") {
      const char* v = need(i, "--dns");
      if (v == nullptr) return -1;
      opt.emit.dns.clear();
      for (const auto& part : subconv::codec::split(v, ',')) {
        const std::string id = subconv::codec::trim(part);
        if (!id.empty()) opt.emit.dns.push_back(id);
      }
    } else if (arg == "--ipv6") {
      opt.emit.ipv6 = true;
    } else if (arg == "--no-ipv6") {
      opt.emit.ipv6 = false;
    } else if (arg == "--probe-cert") {
      // 探测节点证书指纹：需要联网，转换器平时只做本地转换，所以默认关闭
      opt.emit.probe_cert = true;
    } else if (arg == "--probe-cert-timeout") {
      long value = 0;
      if (!need_long(i, "--probe-cert-timeout", value)) return -1;
      if (value < 1 || value > 120) {
        error = "选项 --probe-cert-timeout 需要在 1-120 秒之间，收到: " + std::to_string(value);
        return -1;
      }
      opt.emit.probe_cert_timeout_seconds = static_cast<int>(value);
    } else if (arg == "--list-dns") {
      opt.list_dns = true;
    } else if (arg == "--chain" || arg == "--front-proxy") {
      const char* v = need(i, "--chain");
      if (v == nullptr) return -1;
      // 可重复、按「最外侧 → 最内侧」的顺序；值要么是分享链接，要么是订阅里的节点名。
      // argv 在 Windows 上是 ACP(GBK)，节点名可能是中文，先转成 UTF-8（同 --name）。
      const std::string item = subconv::codec::trim(subconv::console::ansi_to_utf8(v));
      if (!item.empty()) opt.emit.chain.push_back(item);
    } else if (arg == "--chain-rear" || arg == "--rear-proxy") {
      const char* v = need(i, arg.c_str());
      if (v == nullptr) return -1;
      // 可重复、按「最内侧 → 最外侧」的顺序（第 0 项紧跟在节点后面）。
      // 每项都会按节点克隆一份，所以 N 个节点 × m 跳会多出 N×m 个出站。
      const std::string item = subconv::codec::trim(subconv::console::ansi_to_utf8(v));
      if (!item.empty()) opt.emit.chain_rear.push_back(item);
    } else if (arg == "--name") {
      const char* v = need(i, "--name");
      if (v == nullptr) return -1;
      // argv 在 Windows 上是 ACP(GBK) 编码，而这个名字要写进配置载荷，先转成 UTF-8
      opt.emit.filename = subconv::console::ansi_to_utf8(subconv::codec::trim(v));
    } else if (arg == "--proxy") {
      const char* v = need(i, "--proxy");
      if (v == nullptr) return -1;
      opt.load.http.proxy = v;
    } else if (arg == "--ua" || arg == "--user-agent") {
      const char* v = need(i, "--ua");
      if (v == nullptr) return -1;
      opt.load.http.user_agent = v;
    } else if (arg == "--timeout") {
      if (!need_long(i, "--timeout", opt.load.http.timeout_seconds)) return -1;
    } else if (arg == "--retries") {
      long value = 0;
      if (!need_long(i, "--retries", value)) return -1;
      opt.load.http.retries = static_cast<int>(value);
    } else if (arg == "-k" || arg == "--insecure") {
      opt.load.http.insecure = true;
    } else if (arg == "--header") {
      const char* v = need(i, "--header");
      if (v == nullptr) return -1;
      const std::string text = v;
      const auto colon = text.find(':');
      if (colon == std::string::npos) {
        error = "选项 --header 需要 K: V 形式，收到: " + text;
        return -1;
      }
      opt.load.http.headers[subconv::codec::trim(text.substr(0, colon))] =
          subconv::codec::trim(text.substr(colon + 1));
    } else if (arg == "--cache-dir") {
      const char* v = need(i, "--cache-dir");
      if (v == nullptr) return -1;
      opt.load.cache_dir = v;
    } else if (arg == "--cache-ttl") {
      if (!need_long(i, "--cache-ttl", opt.load.cache_ttl_seconds)) return -1;
    } else if (arg == "--no-cache") {
      opt.load.no_cache = true;
    } else if (arg == "-v" || arg == "--verbose") {
      opt.verbose = true;
    } else if (!arg.empty() && arg[0] == '-') {
      error = "未知选项: " + arg;
      return -1;
    } else {
      opt.inputs.push_back(arg);
    }
  }
  opt.load.verbose = opt.verbose;
  opt.load.http.verbose = opt.verbose;
  return 0;
}

subconv::Result<void> write_file(const std::string& path, const std::string& text) {
  const std::string parent = subconv::fs::parent_directory(path);
  if (!parent.empty() && !subconv::fs::exists(parent)) {
    if (!subconv::fs::make_directories(parent)) {
      return subconv::fail("无法创建输出目录: " + parent);
    }
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return subconv::fail("无法写入文件: " + path);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out) return subconv::fail("写入文件失败: " + path);
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  std::string error;
  if (parse_args(argc, argv, opt, error) != 0) {
    subconv::console::write_line(stderr, "错误: " + error);
    subconv::console::write(stderr, "\n");
    print_usage(stderr);
    return 2;
  }

  if (opt.show_help) {
    print_usage(stdout);
    return 0;
  }
  if (opt.show_version) {
    subconv::console::write_line(stdout, std::string("subconv ") + kVersion);
    subconv::console::write_line(stdout,
                                 "已实现目标: " + join_plain(subconv::implemented_targets()));
    subconv::console::write_line(stdout, "规划中目标: " + join_plain(subconv::planned_targets()));
    subconv::console::write_line(
        stdout, std::string("URL 抓取: ") +
                    (subconv::fetch::http_available() ? "可用" : "不可用（未启用 libcurl）"));
    if (subconv::fetch::http_available()) {
      const std::string bundle = subconv::fetch::resolved_ca_bundle();
      subconv::console::write_line(
          stdout, "TLS CA: " + (bundle.empty() ? std::string("libcurl 默认") : bundle));
    }
    return 0;
  }
  if (opt.list_targets) {
    subconv::console::write_line(stdout, "已实现: " + join_plain(subconv::implemented_targets()));
    subconv::console::write_line(stdout, "规划中: " + join_plain(subconv::planned_targets()));
    return 0;
  }
  if (opt.list_rulesets) {
    subconv::console::write_line(stdout, "内置规则集（clash 目标的 rules 段）：");
    for (const auto& rs : subconv::rule_set_catalogue()) {
      std::string id = rs.id;
      if (id.size() < 12) id.append(12 - id.size(), ' ');
      subconv::console::write_line(stdout, "  " + id + rs.name + " [" + rs.policy + "]  " + rs.note);
    }
    subconv::console::write_line(stdout,
                                 "默认: " + join_plain(subconv::default_rule_sets()) +
                                     "（用 --rulesets a,b 覆盖；给空串只留 MATCH 兜底）");
    // 自定义规则集一并列出来，且必须**排在默认值提示之前**说明清楚：
    // 它们只在本次转换里生效，没有"默认选中"的概念。
    if (!opt.emit.custom_rule_sets.empty()) {
      subconv::console::write_line(stdout, "自定义规则集（--ruleset-json / --ruleset-file）：");
      for (const auto& set : opt.emit.custom_rule_sets) {
        std::string id = set.id;
        if (id.size() < 12) id.append(12 - id.size(), ' ');
        subconv::console::write_line(stdout, "  " + id + set.name + " [" + set.policy + "]  " +
                                                 std::to_string(set.rules.size()) + " 条规则");
      }
      subconv::console::write_line(stdout, "自定义集随转换请求一起提交即生效，无需再写进 --rulesets。");
    } else {
      subconv::console::write_line(
          stdout, "自定义规则集：用 --ruleset-json '<JSON>' 或 --ruleset-file <文件> 添加，"
                  "加了就会生效（--rulesets 只管内置集的挑选）。");
    }
    return 0;
  }
  if (opt.list_dns) {
    subconv::console::write_line(stdout, "可选 DNS 预设（clash 目标的 dns.nameserver）：");
    for (const auto& preset : subconv::dns_catalogue()) {
      std::string id = preset.id;
      if (id.size() < 12) id.append(12 - id.size(), ' ');
      std::string line = "  " + id + preset.name + " [" + preset.region + "]  " + preset.note;
      line += "  ->  " + join_plain(preset.v4);
      if (!preset.v6.empty()) line += "  (+IPv6)";
      subconv::console::write_line(stdout, line);
    }
    subconv::console::write_line(
        stdout, "默认: " + join_plain(subconv::default_dns()) +
                    "（用 --dns a,b 覆盖；也可直接填 IP 或 https://<IP>/dns-query；给空串则不写 nameserver）");
    return 0;
  }

  if (opt.serve) {
    subconv::server::ServerOptions server;
    server.listen = opt.listen;
    server.port = opt.port;
    server.open_browser = opt.open_browser;
    server.verbose = opt.verbose;
    server.load = opt.load;
    server.load.verbose = opt.verbose;
    server.load.http.verbose = opt.verbose;
    auto served = subconv::server::run(server);
    if (!served) {
      subconv::console::write_line(stderr, "错误: " + served.error().message);
      return 1;
    }
    return 0;
  }

  if (opt.inputs.empty()) {
    subconv::console::write_line(stderr, "错误: 未指定输入（-i <文件|URL>）");
    subconv::console::write(stderr, "\n");
    print_usage(stderr);
    return 2;
  }

  subconv::NodeList all_nodes;
  subconv::SubscriptionInfo merged_info;
  for (const auto& input : opt.inputs) {
    auto sub = subconv::fetch::load_source(input, opt.load);
    if (!sub) {
      subconv::console::write_line(stderr, "错误: 加载 " + input + " 失败: " + sub.error().message);
      return 1;
    }
    if (opt.verbose) {
      for (const auto& w : sub->warnings) {
        subconv::console::write_line(stderr, "告警 [" + input + "]: " + w);
      }
    } else if (!sub->warnings.empty()) {
      subconv::console::write_line(
          stderr, "告警: " + input + " 有 " + std::to_string(sub->warnings.size()) +
                      " 条内容被跳过（用 -v 查看明细）");
    }
    if (sub->info.has_any()) {
      merged_info.upload += sub->info.upload;
      merged_info.download += sub->info.download;
      merged_info.total += sub->info.total;
      merged_info.expire = std::max(merged_info.expire, sub->info.expire);
    }
    subconv::console::write_line(
        stderr, "已从 " + input + " 解析 " + std::to_string(sub->nodes.size()) + " 个节点");
    all_nodes.insert(all_nodes.end(), std::make_move_iterator(sub->nodes.begin()),
                     std::make_move_iterator(sub->nodes.end()));
  }

  std::vector<std::string> emit_warnings;
  if (opt.emit.probe_cert) {
    const std::size_t probed = subconv::fetch::probe_node_certificates(
        all_nodes, opt.emit.probe_cert_timeout_seconds, &emit_warnings);
    if (opt.verbose || probed > 0) {
      subconv::console::write_line(stderr, "已探测 " + std::to_string(probed) +
                                               " 个证书指纹（--probe-cert）");
    }
  }
  auto output = subconv::emit_config(all_nodes, opt.emit, &emit_warnings);
  if (!output) {
    subconv::console::write_line(stderr, "错误: 生成配置失败: " + output.error().message);
    return 1;
  }
  for (const auto& warning : emit_warnings) {
    subconv::console::write_line(stderr, "告警: " + warning);
  }

  if (opt.output.empty()) {
    // 注意：配置载荷始终写原始 UTF-8 字节 —— 它要落盘或交给内核，
    // 绝不能跟着控制台代码页走（上面的人读提示才需要适配）。
    std::fwrite(output->data(), 1, output->size(), stdout);
  } else {
    auto written = write_file(opt.output, *output);
    if (!written) {
      subconv::console::write_line(stderr, "错误: " + written.error().message);
      return 1;
    }
    subconv::console::write_line(
        stderr, "已写入 " + opt.output + "（" + std::to_string(output->size()) + " 字节）");
  }

  if (merged_info.has_any()) {
    std::string line = "订阅流量: 已用 " +
                       human_bytes(merged_info.upload + merged_info.download) + " / 共 " +
                       human_bytes(merged_info.total);
    if (merged_info.expire > 0) {
      const std::time_t expiry = static_cast<std::time_t>(merged_info.expire);
      char buffer[64] = {0};
      std::tm tm_value{};
#ifdef _WIN32
      if (gmtime_s(&tm_value, &expiry) == 0) {
#else
      if (gmtime_r(&expiry, &tm_value) != nullptr) {
#endif
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm_value);
      }
      line += std::string("，到期 ") + buffer;
    }
    subconv::console::write_line(stderr, line);
  }
  return 0;
}
