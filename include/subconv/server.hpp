// HTTP 服务 + 内嵌 Web UI（M5）
//
// 刻意分成三层，让绝大部分逻辑可以脱离 socket 单测：
//   1. request_from_query / request_from_json —— 协议层：HTTP 参数 → ConvertRequest（纯函数）
//   2. convert                                —— 核心层：来源/内容 → 配置文本（不碰 socket）
//   3. run                                    —— 传输层：极简 HTTP/1.1 服务 + 路由
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "subconv/convert.hpp"
#include "subconv/error.hpp"
#include "subconv/fetch.hpp"
#include "subconv/types.hpp"

namespace subconv::server {

// ---------------------------------------------------------------------------
// 转换
// ---------------------------------------------------------------------------
struct ConvertRequest {
  EmitOptions emit;                   ///< 输出选项；emit.target 决定目标
  std::vector<std::string> sources;   ///< 订阅 URL，或本地文件路径
  std::string content;                ///< 直接粘贴的订阅文本（分享链接 / Base64 / Clash YAML）
  fetch::LoadOptions load;            ///< 抓取选项（代理 / UA / 超时 / 缓存 ...）
  std::string filename;               ///< 订阅名（浏览器另存为 / 客户端识别用的文件名）；为空则按目标自动生成
};

struct ConvertResult {
  std::string config;
  std::string filename;
  std::size_t nodes = 0;              ///< 解析到的节点数（去重前）
  std::vector<std::string> warnings;
  SubscriptionInfo info;
};

/// 目标对应的默认下载文件名，如 subconv-clash.yaml / subconv-xray.json。
[[nodiscard]] std::string default_filename(std::string_view target);

/// 订阅名 → 下载文件名：剔除 Windows 非法字符、按目标补扩展名（本身就带扩展名的原样保留）；
/// name 为空或清洗后为空时返回 default_filename(target)。
[[nodiscard]] std::string filename_for(std::string_view name, std::string_view target);

/// 执行一次转换。除远程来源的抓取外没有任何副作用，便于单测。
[[nodiscard]] Result<ConvertResult> convert(const ConvertRequest& req);

// ---------------------------------------------------------------------------
// 服务
// ---------------------------------------------------------------------------
struct ServerOptions {
  std::string listen = "127.0.0.1";   ///< 监听地址；默认只绑本机
  int port = 25500;                   ///< 0 表示由系统分配空闲端口
  std::string default_target = "clash";
  bool open_browser = false;
  bool verbose = false;
  fetch::LoadOptions load;            ///< 服务端默认抓取参数
};

/// `/sub?...` 查询串 → 转换请求。
[[nodiscard]] Result<ConvertRequest> request_from_query(std::string_view query,
                                                        const ServerOptions& defaults);

/// `POST /api/convert` 的 JSON 请求体 → 转换请求。
[[nodiscard]] Result<ConvertRequest> request_from_json(std::string_view body,
                                                       const ServerOptions& defaults);

/// 从 URL 路径推断目标：`/clash`、`/xray`、`/sing-box` 等；`/sub` 或无法识别时返回空串。
[[nodiscard]] std::string target_from_path(std::string_view path);

/// 阻塞运行 HTTP 服务（Ctrl+C 结束）。
/// 启动后会往 stdout 打印一行 `Web UI: http://<host>:<port>/`，便于脚本探测实际端口。
[[nodiscard]] Result<void> run(const ServerOptions& opts);

}  // namespace subconv::server
