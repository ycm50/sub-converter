// 校验 data/web/index.html 里 splitInput() 的行为（node tools/check-web-input.mjs）：
// 只有「顶格的裸 http(s) 链接行」才算订阅链接，其余行必须连缩进原样保留 ——
// 一旦整段去缩进，Clash YAML 就会变成「yaml-cpp: error at line N: end of map not found」
// （真实踩过的坑，所以在这里钉一颗钉子）。
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const html = fs.readFileSync(path.join(root, 'data', 'web', 'index.html'), 'utf8');

// 从 index.html 里抠出 splitInput() 再执行：不复制一份逻辑，避免两边不同步
const match = html.match(/function splitInput\(text\) \{[\s\S]*?\n  \}/);
if (!match) {
  console.error('未在 data/web/index.html 里找到 splitInput()');
  process.exit(1);
}
const splitInput = new Function('return ' + match[0])();

let failed = 0;
function check(name, ok, detail) {
  if (ok) {
    console.log('  ok   ' + name);
    return;
  }
  failed++;
  console.error('  FAIL ' + name + (detail === undefined ? '' : ' -> ' + detail));
}

console.log('Web UI 输入框切分：');

// 1) 缩进必须原样保留（核心回归点）
{
  const yaml = 'proxies:\n  - name: a\n    ws-opts:\n      path: /x\n    headers:\n      Host: h\n';
  const out = splitInput(yaml);
  check('YAML 缩进原样保留', out.content === yaml, JSON.stringify(out.content));
  check('YAML 不产生订阅链接', out.sources.length === 0, JSON.stringify(out.sources));
}

// 2) 顶格裸链接行 → 订阅链接
{
  const out = splitInput('https://a.example/sub\nhttps://b.example/sub\n');
  check('顶格链接行算订阅链接', out.sources.length === 2 && out.content.trim() === '',
    JSON.stringify(out.sources));
}

// 3) 带缩进的 URL 行仍属于内容（配置文件里的 url 字段很常见）
{
  const out = splitInput('geox-url:\n  mmdb: "https://example.com/geoip.metadb"\n');
  check('缩进过的 URL 行留在内容里',
    out.sources.length === 0 && out.content.includes('  mmdb:'), JSON.stringify(out.sources));
}

// 4) 混合输入：裸链接行抽走，配置缩进不丢
{
  const out = splitInput('https://a.example/sub\nmixed-port: 7890\nproxies:\n  - name: a\n');
  check('混合输入：链接进来源', out.sources.length === 1, JSON.stringify(out.sources));
  check('混合输入：配置缩进不丢',
    out.content.includes('\n  - name: a') && !out.content.includes('https://'),
    JSON.stringify(out.content));
}

// 5) 纯空白输入
check('空白输入无内容', splitInput('   \n\n').content.trim() === '');

console.log(failed === 0 ? '全部通过' : failed + ' 项失败');
process.exit(failed === 0 ? 0 : 1);
