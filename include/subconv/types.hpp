// 中间数据模型：所有协议解析后统一落到 ProxyNode。
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace subconv {

// ---------------------------------------------------------------------------
// 协议
// ---------------------------------------------------------------------------
enum class Protocol {
  Unknown = 0,
  Socks5,
  Http,
  Shadowsocks,
  ShadowsocksR,
  Vmess,
  Vless,
  Trojan,
  Hysteria,
  Hysteria2,
  Tuic,
  Snell,
  WireGuard,
};

/// 规范名（用于 URI scheme 与 Clash `type` 字段）。
[[nodiscard]] const char* to_string(Protocol p) noexcept;
/// Clash `type` 字段名，与 to_string 基本一致，个别协议不同。
[[nodiscard]] const char* clash_type(Protocol p) noexcept;
[[nodiscard]] std::optional<Protocol> protocol_from_string(std::string_view s) noexcept;
[[nodiscard]] bool protocol_is_dialer(Protocol p) noexcept;

// ---------------------------------------------------------------------------
// 传输层
// ---------------------------------------------------------------------------
enum class Network {
  Tcp = 0,
  Ws,
  Grpc,
  H2,
  Http,
  Quic,
  Kcp,
};

[[nodiscard]] const char* to_string(Network n) noexcept;
[[nodiscard]] std::optional<Network> network_from_string(std::string_view s) noexcept;

struct TlsOptions {
  bool enabled = false;
  bool insecure = false;            ///< skip-cert-verify / allowInsecure
  bool disable_sni = false;
  bool reality = false;             ///< VLESS + REALITY
  std::string sni;                  ///< server-name
  std::vector<std::string> alpn;
  std::string fingerprint;          ///< 证书指纹（sha256 pin）
  std::string client_fingerprint;   ///< uTLS 指纹：chrome / firefox / ...
  std::string reality_public_key;   ///< public-key
  std::string reality_short_id;     ///< short-id
};

struct WsOptions {
  std::string path;
  std::string host;                                  ///< Host 头
  std::map<std::string, std::string> headers;        ///< 额外请求头
  std::optional<int> max_early_data;
  std::string early_data_header;
};

struct GrpcOptions {
  std::string service_name;
  bool multi_mode = false;
};

struct H2Options {
  std::string path;
  std::vector<std::string> host;
};

/// Shadowsocks 插件（obfs-local / v2ray-plugin / shadow-tls ...）
struct SsPlugin {
  bool present = false;
  std::string name;                 ///< 原始插件名
  std::string obfs_mode;            ///< obfs=http|tls
  std::string obfs_host;
  std::string mode;                 ///< v2ray-plugin: websocket|quic
  std::string host;
  std::string path;
  std::string tls_host;
  bool tls = false;
  std::string raw;                  ///< 原始 plugin= 字符串
};

// ---------------------------------------------------------------------------
// 节点
// ---------------------------------------------------------------------------
struct ProxyNode {
  Protocol protocol = Protocol::Unknown;
  std::string name;
  std::string server;
  uint16_t port = 0;

  // 凭据
  std::string uuid;
  std::string password;
  std::string username;
  std::string cipher;               ///< ss/ssr 加密方式；vmess security

  // 协议特有
  int alter_id = 0;                 ///< vmess
  std::string flow;                 ///< vless: xtls-rprx-vision
  std::string packet_encoding;      ///< xray: xudp / packetaddr
  std::string ssr_protocol;         ///< ssr: origin / auth_chain_a / auth_aes128_md5 ...
  std::string ssr_protocol_param;
  std::string ssr_obfs;             ///< ssr: plain / http_simple / tls1.2_ticket_auth ...
  std::string ssr_obfs_param;
  std::string peer;                 ///< hysteria
  std::string obfs;                 ///< hysteria2: salamander
  std::string obfs_password;
  std::string up;                   ///< 带宽
  std::string down;
  std::string congestion_control;   ///< tuic
  std::string udp_relay_mode;       ///< tuic
  int version = 0;                  ///< snell

  Network network = Network::Tcp;
  TlsOptions tls;
  WsOptions ws;
  GrpcOptions grpc;
  H2Options h2;
  SsPlugin plugin;

  bool udp = true;
  bool tfo = false;
  bool mptcp = false;
  bool scv = false;                 ///< 单项跳过证书校验（覆盖 tls.insecure）

  /// 已解码但尚未建模的字段，避免信息丢失。
  std::map<std::string, std::string> extra;

  [[nodiscard]] bool is_tls() const noexcept {
    return tls.enabled || protocol == Protocol::Trojan || protocol == Protocol::Hysteria2 ||
           protocol == Protocol::Tuic || protocol == Protocol::Hysteria;
  }
};

using NodeList = std::vector<ProxyNode>;

// ---------------------------------------------------------------------------
// 订阅级信息
// ---------------------------------------------------------------------------
struct SubscriptionInfo {
  int64_t upload = 0;
  int64_t download = 0;
  int64_t total = 0;
  int64_t expire = 0;   ///< Unix 时间戳

  [[nodiscard]] bool has_any() const noexcept {
    return upload > 0 || download > 0 || total > 0 || expire > 0;
  }
};

struct Subscription {
  NodeList nodes;
  SubscriptionInfo info;
  std::string source;        ///< 来源 URL 或文件名（用于诊断）
  std::vector<std::string> warnings;
};

}  // namespace subconv
