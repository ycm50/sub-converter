#!/usr/bin/env bash
# 打包 Windows 产物（单个静态 exe，不用带 DLL）。
#
#   SUBCONV_TAG=v1.0 tools/package-windows.sh windows-x86_64 [构建目录]
#
# 产出：dist/subconv-<tag>-<platform>.zip
# 解包后是 dist/subconv-<tag>-<platform>/ 里面的内容：subconv.exe + README.md + HOW-TO-RUN.txt
#
# 环境变量：
#   SUBCONV_TAG   版本标签（默认 dev）
#   SUBCONV_EXE   可执行文件路径（默认 <构建目录>/subconv.exe）
set -euo pipefail

platform="${1:?用法: SUBCONV_TAG=v1.0 tools/package-windows.sh <platform> [构建目录]}"
build_dir="${2:-build-win}"

tag="${SUBCONV_TAG:-dev}"
exe="${SUBCONV_EXE:-$build_dir/subconv.exe}"

if [ ! -f "$exe" ]; then
  echo "打包失败：找不到可执行文件 $exe" >&2
  exit 1
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
name="subconv-${tag}-${platform}"
stage="$root/dist/${name}"

rm -rf "$stage"
mkdir -p "$stage"
install -m 0755 "$exe" "$stage/subconv.exe"
if [ -f "$root/README.md" ]; then
  install -m 0644 "$root/README.md" "$stage/README.md"
fi

cat > "$stage/HOW-TO-RUN.txt" <<EOF
subconv ${tag} — ${platform}
构建时间（UTC）：$(date -u '+%Y-%m-%d %H:%M:%S')

运行（解压后进这个目录，或在资源管理器里双击）：
  subconv.exe version
  subconv.exe -i https://example.com/sub -t clash -o config.yaml
  subconv.exe serve --open          # 图形界面 + HTTP 接口

这个产物是静态链接的：libcurl / OpenSSL / libc++ 都在 exe 里，
不需要另外装运行库，也不需要 exe 旁边有 DLL。用法与选项见同目录 README.md。
EOF

mkdir -p "$root/dist"
( cd "$root/dist" && rm -f "${name}.zip" && zip -q -r "${name}.zip" "${name}" )

size=$(wc -c < "$root/dist/${name}.zip")
echo "打包完成：dist/${name}.zip（${size} 字节）"
