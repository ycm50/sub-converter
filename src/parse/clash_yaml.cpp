// Clash / mihomo YAML 作为输入源 → ProxyNode
//
// 覆盖常见机场直接下发的 Clash 配置（ss / ssr / vmess / vless / trojan / hysteria /
// hysteria2 / tuic / snell / socks5 / http）。
#include <string>
#include <vector>

#include "parsers.hpp"
#include "subconv/codec.hpp"
#include "subconv/convert.hpp"

#ifdef SUBCONV_HAVE_YAML
#include <yaml-cpp/yaml.h>
#endif

namespace subconv {
namespace {

#ifdef SUBCONV_HAVE_YAML

using YamlNode = YAML::Node;

std::string ystr(const YamlNode& n, const char* key) {
  const YamlNode v = n[key];
  if (!v || !v.IsScalar()) return {};
  try {
    return v.as<std::string>();
  } catch (...) {
    return {};
  }
}

std::string ystr_any(const YamlNode& n, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const std::string v = ystr(n, key);
    if (!v.empty()) return v;
  }
  return {};
}

int yint(const YamlNode& n, const char* key, int fallback = 0) {
  const std::string v = ystr(n, key);
  if (v.empty()) return fallback;
  try {
    return std::stoi(v);
  } catch (...) {
    return fallback;
  }
}

bool ybool(const YamlNode& n, const char* key, bool fallback = false) {
  const YamlNode v = n[key];
  if (!v || !v.IsScalar()) return fallback;
  try {
    const std::string s = codec::to_lower(v.as<std::string>());
    return s == "true" || s == "yes" || s == "on" || s == "1";
  } catch (...) {
    return fallback;
  }
}

std::vector<std::string> ylist(const YamlNode& n) {
  std::vector<std::string> out;
  if (!n) return out;
  if (n.IsSequence()) {
    for (const auto& item : n) {
      if (item.IsScalar()) {
        try {
          out.push_back(item.as<std::string>());
        } catch (...) {
        }
      }
    }
  } else if (n.IsScalar()) {
    try {
      out.push_back(n.as<std::string>());
    } catch (...) {
    }
  }
  return out;
}

void apply_tls_from_yaml(ProxyNode& node, const YamlNode& p) {
  const std::string sni = ystr_any(p, {"sni", "servername", "server-name"});
  if (!sni.empty()) node.tls.sni = sni;
  if (ybool(p, "skip-cert-verify")) node.tls.insecure = true;
  if (const auto alpn = ylist(p["alpn"]); !alpn.empty()) node.tls.alpn = alpn;
  const std::string fp = ystr_any(p, {"client-fingerprint"});
  if (!fp.empty()) node.tls.client_fingerprint = fp;
  const std::string pin = ystr_any(p, {"fingerprint"});
  if (!pin.empty() && pin.find(':') != std::string::npos) node.tls.fingerprint = pin;
  if (ybool(p, "tls")) node.tls.enabled = true;

  const YamlNode reality = p["reality-opts"];
  if (reality && reality.IsMap()) {
    node.tls.enabled = true;
    node.tls.reality = true;
    node.tls.reality_public_key = ystr_any(reality, {"public-key", "public_key"});
    node.tls.reality_short_id = ystr_any(reality, {"short-id", "short_id"});
  }
}

void apply_transport_from_yaml(ProxyNode& node, const YamlNode& p) {
  const std::string network = codec::to_lower(ystr(p, "network"));
  if (network.empty() || network == "tcp") {
    node.network = Network::Tcp;
    return;
  }
  if (auto n = network_from_string(network)) node.network = *n;

  switch (node.network) {
    case Network::Ws: {
      const YamlNode ws = p["ws-opts"];
      if (ws && ws.IsMap()) {
        node.ws.path = ystr(ws, "path");
        const YamlNode headers = ws["headers"];
        if (headers && headers.IsMap()) {
          node.ws.host = ystr_any(headers, {"Host", "host"});
          for (const auto& kv : headers) {
            const std::string key = kv.first.as<std::string>();
            if (key != "Host" && key != "host" && kv.second.IsScalar()) {
              node.ws.headers[key] = kv.second.as<std::string>();
            }
          }
        }
        const int early = yint(ws, "max-early-data", -1);
        if (early >= 0) node.ws.max_early_data = early;
        node.ws.early_data_header = ystr(ws, "early-data-header-name");
      }
      break;
    }
    case Network::Grpc: {
      const YamlNode grpc = p["grpc-opts"];
      if (grpc && grpc.IsMap()) {
        node.grpc.service_name = ystr_any(grpc, {"grpc-service-name", "serviceName"});
        const std::string mode = codec::to_lower(ystr(grpc, "grpc-mode"));
        if (mode == "multi") node.grpc.multi_mode = true;
      }
      break;
    }
    case Network::H2: {
      const YamlNode h2 = p["h2-opts"];
      if (h2 && h2.IsMap()) {
        node.h2.host = ylist(h2["host"]);
        node.h2.path = ystr(h2, "path");
      }
      break;
    }
    case Network::Http: {
      const YamlNode http = p["http-opts"];
      if (http && http.IsMap()) {
        const auto paths = ylist(http["path"]);
        if (!paths.empty()) node.h2.path = paths.front();
        const YamlNode headers = http["headers"];
        if (headers && headers.IsMap()) node.h2.host = ylist(headers["Host"]);
      }
      break;
    }
    default:
      break;
  }
}

Result<ProxyNode> proxy_from_yaml(const YamlNode& p) {
  const std::string type = codec::to_lower(ystr(p, "type"));
  if (type.empty()) return fail("proxy 缺少 type 字段");

  auto protocol = protocol_from_string(type);
  if (!protocol) return fail("不支持的 proxy 类型: " + type);

  ProxyNode node;
  node.protocol = *protocol;
  node.name = ystr(p, "name");
  node.server = ystr(p, "server");
  const int port = yint(p, "port", 0);
  if (node.server.empty() || port <= 0 || port > 65535) {
    return fail("proxy 缺少合法的 server/port");
  }
  node.port = static_cast<uint16_t>(port);
  if (ybool(p, "udp", true) == false) node.udp = false;
  if (ybool(p, "tfo")) node.tfo = true;

  switch (*protocol) {
    case Protocol::Shadowsocks: {
      node.cipher = ystr(p, "cipher");
      node.password = ystr(p, "password");
      const std::string plugin = codec::to_lower(ystr(p, "plugin"));
      if (!plugin.empty()) {
        node.plugin.present = true;
        node.plugin.name = plugin;
        const YamlNode opts = p["plugin-opts"];
        if (opts && opts.IsMap()) {
          node.plugin.obfs_mode = ystr(opts, "mode");
          node.plugin.mode = node.plugin.obfs_mode;
          node.plugin.host = ystr(opts, "host");
          node.plugin.obfs_host = node.plugin.host;
          node.plugin.path = ystr(opts, "path");
          node.plugin.tls = ybool(opts, "tls");
        }
      }
      break;
    }
    case Protocol::ShadowsocksR: {
      node.cipher = ystr(p, "cipher");
      node.password = ystr(p, "password");
      node.ssr_protocol = ystr(p, "protocol");
      node.ssr_obfs = ystr(p, "obfs");
      node.ssr_protocol_param = ystr_any(p, {"protocol-param", "protocol_param"});
      node.ssr_obfs_param = ystr_any(p, {"obfs-param", "obfs_param"});
      break;
    }
    case Protocol::Vmess: {
      node.uuid = ystr(p, "uuid");
      node.alter_id = yint(p, "alterId", 0);
      node.cipher = ystr(p, "cipher");
      apply_transport_from_yaml(node, p);
      apply_tls_from_yaml(node, p);
      break;
    }
    case Protocol::Vless: {
      node.uuid = ystr(p, "uuid");
      node.flow = ystr(p, "flow");
      apply_transport_from_yaml(node, p);
      apply_tls_from_yaml(node, p);
      node.tls.enabled = true;  // vless 在 Clash 里恒为 TLS（含 reality）
      break;
    }
    case Protocol::Trojan: {
      node.password = ystr(p, "password");
      apply_transport_from_yaml(node, p);
      apply_tls_from_yaml(node, p);
      node.tls.enabled = true;
      break;
    }
    case Protocol::Hysteria: {
      node.password = ystr_any(p, {"auth-str", "auth_str", "auth"});
      node.up = ystr(p, "up");
      node.down = ystr(p, "down");
      node.obfs = ystr(p, "obfs");
      const std::string proto = ystr(p, "protocol");
      if (!proto.empty()) node.extra["protocol"] = proto;
      apply_tls_from_yaml(node, p);
      node.tls.enabled = true;
      break;
    }
    case Protocol::Hysteria2: {
      node.password = ystr_any(p, {"password", "auth"});
      node.up = ystr(p, "up");
      node.down = ystr(p, "down");
      node.obfs = ystr(p, "obfs");
      node.obfs_password = ystr_any(p, {"obfs-password", "obfs_password"});
      apply_tls_from_yaml(node, p);
      node.tls.enabled = true;
      break;
    }
    case Protocol::Tuic: {
      node.uuid = ystr(p, "uuid");
      node.password = ystr_any(p, {"password", "token"});
      node.congestion_control = ystr_any(p, {"congestion-controller", "congestion_control"});
      node.udp_relay_mode = ystr_any(p, {"udp-relay-mode", "udp_relay_mode"});
      apply_tls_from_yaml(node, p);
      node.tls.enabled = true;
      break;
    }
    case Protocol::Snell: {
      node.password = ystr_any(p, {"psk", "password"});
      node.version = yint(p, "version", 3);
      const YamlNode obfs = p["obfs-opts"];
      if (obfs && obfs.IsMap()) {
        node.obfs = ystr(obfs, "mode");
        node.obfs_password = ystr(obfs, "host");
      }
      break;
    }
    case Protocol::Socks5:
    case Protocol::Http: {
      node.username = ystr(p, "username");
      node.password = ystr(p, "password");
      apply_tls_from_yaml(node, p);
      break;
    }
    default:
      return fail("暂不支持从 Clash YAML 读取该类型");
  }

  return node;
}

#endif  // SUBCONV_HAVE_YAML

}  // namespace

Result<Subscription> parse_clash_yaml(std::string_view yaml, std::string source) {
#ifdef SUBCONV_HAVE_YAML
  Subscription sub;
  sub.source = std::move(source);

  YamlNode root;
  try {
    root = YAML::Load(std::string(yaml));
  } catch (const std::exception& e) {
    return fail(std::string("Clash YAML 解析失败: ") + e.what());
  }
  if (!root || !root.IsMap()) return fail("Clash YAML 根节点不是映射");

  const YamlNode proxies = root["proxies"];
  if (!proxies || !proxies.IsSequence() || proxies.size() == 0) {
    return fail("Clash YAML 中没有可用的 proxies 列表");
  }

  std::size_t index = 0;
  for (const auto& entry : proxies) {
    ++index;
    if (!entry.IsMap()) {
      sub.warnings.push_back("第 " + std::to_string(index) + " 个 proxy 不是映射，已跳过");
      continue;
    }
    auto node = proxy_from_yaml(entry);
    if (!node) {
      const std::string name = ystr(entry, "name");
      sub.warnings.push_back("第 " + std::to_string(index) + " 个 proxy 解析失败" +
                             (name.empty() ? std::string() : "（" + name + "）") + ": " +
                             node.error().message);
      continue;
    }
    if (node->name.empty()) node->name = "节点 " + std::to_string(index);
    sub.nodes.push_back(std::move(*node));
  }

  if (sub.nodes.empty()) return fail("Clash YAML 中没有任何可解析的节点");
  return sub;
#else
  (void)yaml;
  (void)source;
  return fail("本次构建未启用 yaml-cpp，无法把 Clash YAML 作为输入源");
#endif
}

}  // namespace subconv
