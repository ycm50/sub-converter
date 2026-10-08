# 全局测试列表

> **用途**：用户说「全局测试」时，按本列表逐项执行；说「更新测试列表」时才重新生成本文件。
> 平时新增/修改代码**只测改动部分**，不跑全量。
>
> 本文件随代码一起提交，是全局测试的唯一依据。

## 环境

| 项 | 值 |
|---|---|
| 平台 | Windows + MSYS2 UCRT64 |
| 工具链 | `A:\msys64\ucrt64\bin`（g++ 必需，必须置于 PATH 最前） |
| 构建 | CMake + Ninja，配置 `-DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=A:\msys64\ucrt64` |
| 内核 | `tools\bin\`（`tools\validate.ps1 -Setup` 下载；mihomo / Xray / sing-box，目录不入库） |

```powershell
$env:PATH = "A:\msys64\ucrt64\bin;$env:PATH"
```

> ⚠️ 本仓库是 CMake 工程，与 Android 侧的「禁止 gradle 编译」无冲突：这里不涉及 Gradle。

---

## 1. 构建与单元测试

**命令**

```powershell
$env:PATH = "A:\msys64\ucrt64\bin;$env:PATH"
cd A:\Downloads\tell-shell\sub-converter
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="A:\msys64\ucrt64"
cmake --build build
.\build\subconv_tests.exe
```

**通过标准**

- 构建：**0 error / 0 warning**
- 测试：末尾输出 `1507 项断言，0 项失败`、退出码 `0`
- 基线：断言总数只应增加；数字下降说明有测试被删，需确认是有意为之

**覆盖的 24 个 section**（顺序即 `tests/test_main.cpp` 里的调用顺序）：

| # | section | 覆盖内容 |
|---|---|---|
| 1 | `base64` | 标准 / URL-safe / 缺 padding / 两层包裹 / BOM |
| 2 | `percent / uri` | 百分号编解码、URI 解析、host:port |
| 3 | `ss:// 解析` | shadowsocks（含 obfs / v2ray-plugin） |
| 4 | `socks5 / http` | socks5 / http(s) 代理链接 |
| 5 | `yaml 输出` | 手写 YAML 渲染器 |
| 6 | `订阅解析` | 分享链接列表、注释行、空行 |
| 7 | `clash 输出` | clash 目标产物结构 |
| 8 | `分流规则集` | 内置规则集目录、展开顺序、未知 id 告警 |
| 9 | `自定义规则集` | 解析 / 校验 / 展开顺序 / 类型目录 / HTTP 映射（**含 2 条原样回归**） |
| 10 | `DNS / ipv6 / 订阅名` | DNS 预设、ipv6 开关、订阅名 → 文件名 |
| 11 | `高级协议解析` | vmess / vless / trojan / hysteria / hysteria2 / tuic / snell / wireguard |
| 12 | `xray / sing-box 输出` | 两个目标的产物结构 |
| 13 | `内容嗅探 / userinfo` | 订阅内容嗅探、流量信息 |
| 14 | `Clash YAML 作为输入源` | 上游 YAML / JSON 版 Clash 配置 |
| 15 | `Xray JSON 配置作为输入源` | Xray / V2Ray JSON 客户端配置（含配置数组） |
| 16 | `HTTP 服务 / 请求映射` | `request_from_query` / `request_from_json` / 目标推断 |
| 17 | `libcurl / CA bundle` | CA 路径回退 |
| 18 | `控制台输出编码` | 代码页转换（中文不乱码） |
| 19 | `分享链接 / v2rayNG` | links / base64 目标 |
| 20 | `链式代理（前置 / 中转）` | `--chain` 落到 clash / xray / singbox |
| 21 | `链式代理（后置 / 出口）` | `--chain-rear` 的克隆与落点语义 |
| 22 | `v2rayNG 完整格式 (v2rayn://)` | v2rayn 目标 |
| 23 | `XHTTP 传输` | xhttp + download-settings 的三种形态换算 |
| 24 | `VLESS Encryption` | ML-KEM 块的解析、校验与透传 |

---

## 2. 真实内核校验（三目标）

用 mihomo / Xray / sing-box 的**真实二进制**校验产物，这是本项目的核心质量保证。

**首次准备内核**

```powershell
cd A:\Downloads\tell-shell\sub-converter
powershell -ExecutionPolicy Bypass -File tools\validate.ps1 -Setup
```

**逐目标校验**

```powershell
powershell -ExecutionPolicy Bypass -File tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target clash
powershell -ExecutionPolicy Bypass -File tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target xray
powershell -ExecutionPolicy Bypass -File tools\validate.ps1 -Source tests\fixtures\all_protocols_b64.txt -Target singbox
```

**通过标准**：三个目标都输出 `校验通过: <产物路径>`，且内核报告
`configuration file ... test is successful`。

> 该脚本会自动下载 geodata（`geoip.metadb` / `geosite.dat`）到 `tools\bin\`，首次稍慢。

---

## 3. Web UI 结构与回归

**3.1 输入框切分回归**（必须，防「YAML 丢缩进」坑）

```powershell
cd A:\Downloads\tell-shell\sub-converter
node tools\check-web-input.mjs
```

**通过标准**：输出 `全部通过`，退出码 `0`。

**3.2 Web UI 语法检查**

把内嵌的 `<script>` 抽出来做语法检查（`data/web/index.html` 是单文件、只有一个 script 块）：

```powershell
cd A:\Downloads\tell-shell\sub-converter
# 抽出最后一个 <script> 块，交给 node --check
python -c "import re,io; h=io.open('data/web/index.html',encoding='utf-8').read(); m=re.findall(r'<script>(.*?)</script>', h, re.S); io.open('.web-check.js','w',encoding='utf-8',newline='\n').write(m[-1])"
node --check .web-check.js
Remove-Item .web-check.js
```

**通过标准**：`node --check` 退出码 `0`。

**3.3 真实浏览器端到端**（改过 Web UI 时必做）

现有的坑（元素缺失、函数没被调用、规则没进产物）**都只能靠这一项抓到**，纯逻辑单测会漏。
用 Chrome DevTools Protocol 驱动 `--headless=new`，检查：

| 检查点 | 通过标准 |
|---|---|
| 控制台错误 | 无（`window.__errs` 为空） |
| 规则块可见 | 无需展开任何折叠区即 `!hidden` |
| 内置规则集勾选框 | 数量 = 目录里的内置集数量（当前 9） |
| 规则表行数 | = 自定义行 + 勾选展开的内置行 |
| 添加规则 | 入表且**排在最上方**；提交 JSON 里 `type` 是**原始类型名**（未被中文污染） |
| 选中自定义行 | 删除按钮可用；删除后该行消失 |
| 选中内置行 | 删除按钮**置灰**（只读） |
| 值含逗号 | 被拒并显示错误提示 |
| **表格顺序 vs 产物顺序** | 逐行一致（自定义在前、内置在后、末行 `MATCH`） |
| 类型下拉 | 显示「中文（原类型名）」，`option.value` 是原始类型名 |

**3.4 手动核对**：`.\build\subconv.exe serve --open` 打开界面肉眼确认。
> Web UI 是**编译期嵌入**二进制的，改完 `data/web/index.html` 必须重新构建，且**重启 serve**。

---

## 4. CLI 冒烟

```powershell
cd A:\Downloads\tell-shell\sub-converter
.\build\subconv.exe --list-rulesets
.\build\subconv.exe --list-dns
.\build\subconv.exe --list-targets

# 自定义规则集：不写进 --rulesets 也应生效
'{"myads":{"policy":"REJECT","rules":["DOMAIN-KEYWORD,doubleclick"]}}' | Set-Content -Encoding ascii r.json
"ss://YWVzLTI1Ni1nY206c3NwYXNz@ss.example.com:8443#SS" | Out-File -Encoding ascii t.txt
.\build\subconv.exe -i t.txt -t clash --rulesets "local" --ruleset-file r.json
```

**通过标准**：`rules:` 段里出现 `DOMAIN-KEYWORD,doubleclick,REJECT`，且排在**内置规则之前**。

> ⚠️ Windows 命令行把非 ASCII 参数按 ANSI 传给进程：**自定义规则集 id 用 ASCII**，
> 中文 id 走 `--rulesets` 会变成非法 UTF-8（HTTP 通道无此问题）。

---

## 5. 文档与仓库卫生

| 检查 | 命令 / 标准 |
|---|---|
| README 内部锚点 | 新增的链接必须能跳转到对应标题（旧的 3 条失效锚点为历史遗留） |
| `.gitignore` 未误伤 | `git ls-files -i -c --exclude-standard` **输出为空** |
| 无残留临时文件 | `git status --porcelain` 只有预期改动，无 `??` 垃圾文件 |
| 密钥未入库 | 仓库内不应出现任何 `.jks` / `keystore` |

---

## 改动 → 必测项 对照

| 改了什么 | 至少跑哪几项 |
|---|---|
| `rulesets.cpp` / 规则相关 | 1（含 `分流规则集` + `自定义规则集`）+ 2 |
| `data/web/index.html` | 3.1 + 3.2 + 3.3（**必做 3.3**）+ 2 |
| 新增协议 / 传输 | 1（`高级协议解析` + 对应目标 section）+ 2 |
| `src/server/*` | 1（`HTTP 服务 / 请求映射`）+ 3 |
| 仅文档（`*.md`） | 5 |
| `.gitignore` | 5 |
| 构建脚本 / CMake | 1 + 2 |

---

## 已知与注意事项

- **内核二进制不入库**：`tools/bin/` 已被忽略，换机器要重新 `-Setup`（需联网）。
- **Web UI 无热更新**：改界面后不重启 `serve` 会对着旧页面排查。
- **产物顺序 = 规则表顺序**：不按 policy 重排，输出与界面所见一致。
- **自定义规则不跨请求保存**：Web UI 的规则存在浏览器 localStorage，提交时随请求发；
  直接用 `/sub?` 链接必须显式带 `&custom_rulesets=`（只写 `&rulesets=` 不带自定义规则）。
- 断言基线 **1507**；数字变化需说明原因。
