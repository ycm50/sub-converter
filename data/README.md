# data/ 目录说明

本目录存放**内置默认资源**，随程序一起发布：

| 文件 | 用途 | 状态 |
|---|---|---|
| `web/index.html` | Web UI 源文件（`subconv serve` 的界面） | ✅ 已启用 |
| `pref.ini` | 默认配置（默认目标、监听地址、缓存目录） | 计划中 |
| `groups.ini` | 分组模板（手动/自动/地区/流媒体分组） | 计划中 |
| `rules.ini` | 内置规则模板 | ❌ 改为内建：见下 |
| `rulesets/` | 本地规则集目录 | ❌ 改为内建：见下 |

> `web/index.html` 是唯一有运行时作用的内置资源：CMake 在配置阶段把它读成 C++ 原始字符串
> 写进 `build/generated/web_ui.hpp`，所以 Web UI 不依赖任何运行时路径，拷到哪都能跑。
> 改动该文件会触发 CMake 重新配置（`CMAKE_CONFIGURE_DEPENDS`）。

**分流规则集与 DNS 预设没有放到这里**，而是内建在 src/emit/rulesets.cpp 与 src/emit/dns.cpp 的目录表里。原因：

1. 项目里 yaml-cpp 是**可选**依赖（`SUBCONV_HAVE_YAML`），把目录放进 YAML 会让功能随依赖开关而消失；
2. 目录需要被多处共用（CLI --list-rulesets / --list-dns、HTTP GET /api/rulesets / GET /api/dns、生成 rules: 与 dns.nameserver 段），
   放在 C++ 里是唯一真源，Web UI 通过接口取，不会出现两边写死不同步；
3. 规则集只用 mihomo 自带的 GEOSITE/GEOIP，**不依赖远程 rule-provider**（那会让 mihomo 启动时
   必须联网下载，失败则整份配置起不来，且无法离线校验）。

设计原则：**不依赖远程规则集也能产出可用配置**。Clash 目标的分组内建在 `src/emit/clash.cpp`
（3 个分组：节点选择 / 自动选择 / 漏网之鱼），规则由 `src/emit/rulesets.cpp` 按选中的规则集生成；
Xray / sing-box 目标不含分组与规则。`pref.ini` / `groups.ini` 仍待做。
