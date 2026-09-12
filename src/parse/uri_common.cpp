#include "uri_common.hpp"

#include "subconv/codec.hpp"

namespace subconv::parse_detail {

const std::string* pick(const std::map<std::string, std::string>& q,
                        std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    for (const auto& [k, v] : q) {
      if (codec::iequals(k, key)) return &v;
    }
  }
  return nullptr;
}

bool truthy(const std::string* v) {
  if (v == nullptr) return false;
  return *v == "1" || codec::iequals(*v, "true") || codec::iequals(*v, "yes") ||
         codec::iequals(*v, "on");
}

std::vector<std::string> split_alpn(const std::string& s) {
  std::vector<std::string> out;
  for (auto& part : codec::split(s, ',')) {
    const std::string t = codec::trim(part);
    if (!t.empty()) out.push_back(t);
  }
  if (out.empty() && !s.empty()) out.push_back(codec::trim(s));
  return out;
}

std::string name_from_fragment(const std::string& fragment, const std::string& fallback) {
  const std::string name = codec::trim(codec::percent_decode(fragment));
  return name.empty() ? fallback : name;
}

void apply_common_params(ProxyNode& node, const std::map<std::string, std::string>& q) {
  // ---- 传输层（network）----
  std::string header_type;
  if (const auto* v = pick(q, {"headerType", "header-type"})) header_type = *v;

  if (const auto* v = pick(q, {"type", "net", "network"})) {
    if (auto n = network_from_string(*v)) node.network = *n;
  }
  // v2ray 的 tcp + headerType=http 等价于 HTTP 传输层
  if (node.network == Network::Tcp && codec::iequals(header_type, "http")) {
    node.network = Network::Http;
  }
  if (!header_type.empty()) node.extra["headerType"] = header_type;

  // ---- TLS ----
  const std::string* security = pick(q, {"security"});
  if (security != nullptr) {
    if (codec::iequals(*security, "tls") || codec::iequals(*security, "xtls")) {
      node.tls.enabled = true;
    } else if (codec::iequals(*security, "reality")) {
      node.tls.enabled = true;
      node.tls.reality = true;
    } else if (codec::iequals(*security, "none") || codec::iequals(*security, "")) {
      node.tls.enabled = false;
    }
  }
  if (truthy(pick(q, {"tls"}))) node.tls.enabled = true;

  if (const auto* v = pick(q, {"sni", "peer", "serverName", "servername"})) {
    node.tls.sni = *v;
  }
  if (const auto* v = pick(q, {"alpn"})) node.tls.alpn = split_alpn(*v);
  if (const auto* v = pick(q, {"fp", "fingerprint"})) node.tls.client_fingerprint = *v;
  if (const auto* v = pick(q, {"pbk", "publicKey", "public-key"})) {
    node.tls.enabled = true;
    node.tls.reality = true;
    node.tls.reality_public_key = *v;
  }
  if (const auto* v = pick(q, {"sid", "shortId", "short-id"})) {
    node.tls.reality_short_id = *v;
  }
  if (const auto* v = pick(q, {"spx", "spiderX", "spider-x"})) node.extra["spiderX"] = *v;

  if (truthy(pick(q, {"allowInsecure", "insecure", "allow_insecure", "skip-cert-verify",
                      "skipCertVerify"}))) {
    node.tls.insecure = true;
  }
  if (const auto* v = pick(q, {"disableSni", "disable_sni"}); truthy(v)) {
    node.tls.disable_sni = true;
  }

  if (const auto* v = pick(q, {"flow"})) node.flow = *v;
  if (const auto* v = pick(q, {"packetEncoding", "packet-encoding"})) node.packet_encoding = *v;

  // ---- 传输层细节 ----
  const std::string* path = pick(q, {"path"});
  const std::string* host = pick(q, {"host"});
  const std::string* service = pick(q, {"serviceName", "service-name", "servicename"});
  const std::string* mode = pick(q, {"mode"});

  switch (node.network) {
    case Network::Ws: {
      if (path != nullptr) node.ws.path = *path;
      if (host != nullptr) node.ws.host = *host;
      if (const auto* v = pick(q, {"ed", "maxEarlyData"})) {
        try {
          node.ws.max_early_data = std::stoi(*v);
        } catch (...) {
          node.ws.max_early_data = std::nullopt;
        }
      }
      if (const auto* v = pick(q, {"eh", "earlyDataHeaderName"})) {
        node.ws.early_data_header = *v;
      }
      break;
    }
    case Network::Grpc: {
      if (service != nullptr) node.grpc.service_name = *service;
      else if (path != nullptr) node.grpc.service_name = *path;  // 常见误用：grpc 用 path 传 serviceName
      if (mode != nullptr && codec::iequals(*mode, "multi")) node.grpc.multi_mode = true;
      break;
    }
    case Network::H2:
    case Network::Http: {
      if (path != nullptr) node.h2.path = *path;
      if (host != nullptr) node.h2.host = codec::split(*host, ',');
      break;
    }
    default:
      break;
  }
}

}  // namespace subconv::parse_detail
