#!/usr/bin/env bash
# 打包 *nix 产物（Linux / Termux）
#
#   SUBCONV_TAG=v1.0 tools/package.sh linux-x86_64
#   SUBCONV_TAG=v1.0 tools/package.sh termux-aarch64 build build/bin/extra.so
#
# 产出：dist/subconv-<tag>-<platform>.tar.gz
# 解包后是一个同名目录，里面是 subconv + README.md + 同目录的附加文件。
#
# 环境变量：
#   SUBCONV_TAG   版本标签（默认 dev）
#   SUBCONV_BIN   可执行文件路径（默认 <build-dir>/subconv）
#   SUBCONV_DEPS  运行产物需要额外安装的包（写进 HOW-TO-RUN.txt，例如 "libcurl openssl"）
set -euo pipefail

platform="${1:?用法: SUBCONV_TAG=v1.0 tools/package.sh <platform> [build-dir] [extra-file...]}"
shift
build_dir="${1:-build}"
shift || true
extra=("$@")

tag="${SUBCONV_TAG:-dev}"
bin="${SUBCONV_BIN:-$build_dir/subconv}"

if [ ! -f "$bin" ]; then
  echo "打包失败：找不到可执行文件 $bin" >&2
  exit 1
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
name="subconv-${tag}-${platform}"
stage="$root/dist/${name}"

rm -rf "$stage"
mkdir -p "$stage"
install -m 0755 "$bin" "$stage/subconv"
if [ -f "$root/README.md" ]; then
  install -m 0644 "$root/README.md" "$stage/README.md"
fi
for file in ${extra[@]+"${extra[@]}"}; do
  [ -n "$file" ] && install -m 0755 "$file" "$stage/"
done

cat > "$stage/HOW-TO-RUN.txt" <<EOF
subconv ${tag} — ${platform}
构建时间（UTC）：$(date -u '+%Y-%m-%d %H:%M:%S')

运行：
  ./subconv version
  ./subconv -i https://example.com/sub -t clash -o config.yaml
  ./subconv serve --open          # 图形界面 + HTTP 接口

用法与选项见同目录 README.md。
EOF

if [ -n "${SUBCONV_DEPS:-}" ]; then
  cat >> "$stage/HOW-TO-RUN.txt" <<EOF

这个产物是动态链接的，运行前需要这些运行库：
  ${SUBCONV_DEPS}
（Debian/Ubuntu: sudo apt install ${SUBCONV_DEPS}）
EOF
fi

if [ -n "${SUBCONV_NOTES:-}" ]; then
  printf '\n%s\n' "${SUBCONV_NOTES}" >> "$stage/HOW-TO-RUN.txt"
fi

mkdir -p "$root/dist"
( cd "$root/dist" && tar -czf "${name}.tar.gz" "${name}" )

size=$(wc -c < "$root/dist/${name}.tar.gz")
echo "打包完成：dist/${name}.tar.gz（${size} 字节）"
