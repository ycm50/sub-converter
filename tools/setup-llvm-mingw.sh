#!/usr/bin/env bash
# 下载并展开 llvm-mingw（UCRT 版）。
#
#   tools/setup-llvm-mingw.sh [安装目录]        默认 .cache/llvm-mingw
#
# 一份 Linux x86_64 宿主的包里就同时带了 i686 / x86_64 / armv7 / aarch64 四套目标，
# 所以 Windows 的两种架构共用这一个工具链。已经有可用工具链时直接跳过，
# 方便 CI 缓存和本地反复构建。
set -euo pipefail

dest="${1:-.cache/llvm-mingw}"
version="${SUBCONV_LLVM_MINGW_VERSION:-20260908}"
asset="llvm-mingw-${version}-ucrt-ubuntu-22.04-x86_64.tar.xz"
# 国内网络可以套一层 GitHub 镜像前缀，例如 SUBCONV_GH_MIRROR=https://gh-proxy.com/
url="${SUBCONV_GH_MIRROR:-}https://github.com/mstorsjo/llvm-mingw/releases/download/${version}/${asset}"

if [ -x "$dest/bin/x86_64-w64-mingw32-clang++" ] && [ -x "$dest/bin/aarch64-w64-mingw32-clang++" ]; then
  echo "llvm-mingw 已就绪：$dest"
  exit 0
fi

mkdir -p "$dest"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "下载 llvm-mingw ${version}（约 84 MB）：$url"
curl -fSL --retry 5 --retry-delay 3 --retry-all-errors -o "$tmp/$asset" "$url"
tar -xJf "$tmp/$asset" -C "$dest" --strip-components=1

for cxx in x86_64-w64-mingw32-clang++ aarch64-w64-mingw32-clang++; do
  if [ ! -x "$dest/bin/$cxx" ]; then
    echo "解出来的工具链里没有 $cxx，包可能下坏了" >&2
    exit 1
  fi
done

echo "llvm-mingw 装好了：$dest"
"$dest/bin/x86_64-w64-mingw32-clang++" --version | head -1
