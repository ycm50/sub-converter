#!/usr/bin/env bash
# Linux / Termux 构建脚本（等价于 Windows 上的 build.ps1）
#
#   ./build.sh                     # Release 构建
#   ./build.sh --test              # 构建并跑单元测试
#   ./build.sh --clean --test      # 先清空 build 目录
#   ./build.sh --config Debug      # 指定构建类型（默认 Release）
#   ./build.sh --vendor-yaml       # 内置静态编译 yaml-cpp（发行版只有 0.7 时用）
#   ./build.sh --static-runtime    # 静态链接 libstdc++/libgcc
#   ./build.sh --version v1.0      # 覆盖二进制里显示的版本号
#   ./build.sh --prefix /usr/local # 依赖装在非默认前缀时指路（多个用 : 分隔）
#
# 依赖怎么装：
#   Debian/Ubuntu  sudo apt install build-essential cmake ninja-build \
#                                   libcurl4-openssl-dev libssl-dev libyaml-cpp-dev
#   Fedora         sudo dnf install gcc-c++ cmake ninja-build libcurl-devel openssl-devel yaml-cpp-devel
#   Arch           sudo pacman -S base-devel cmake ninja curl openssl yaml-cpp
#   Termux         pkg install clang cmake ninja libcurl openssl yaml-cpp
#
# 三个可选依赖（libcurl / OpenSSL / yaml-cpp）装不全也能编过，只是对应功能停用：
# 详见 CMakeLists.txt 与 README「从源码构建」。
set -euo pipefail

config="Release"
build_dir="build"
clean=0
run_tests=0
user_prefix=""
extra_args=()

usage() {
  # 打印文件头部的注释块：从第 2 行起，遇到第一行非注释就停。
  # （别用 sed 硬编码行号 —— 改注释就得跟着改行号，漏改一次就会把代码当帮助打出来。）
  awk 'NR > 1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$0"
  exit "${1:-0}"
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)        usage 0 ;;
    -c|--clean)       clean=1 ;;
    -t|--test)        run_tests=1 ;;
    --config)         config="${2:?--config 需要参数}"; shift ;;
    --build-dir)      build_dir="${2:?--build-dir 需要参数}"; shift ;;
    --prefix)         user_prefix="${2:?--prefix 需要参数}"; shift ;;
    --version|-V)     extra_args+=("-DSUBCONV_VERSION:STRING=${2:?--version 需要参数}"); shift ;;
    --vendor-yaml)    extra_args+=("-DSUBCONV_VENDOR_YAMLCPP=ON") ;;
    --static-runtime) extra_args+=("-DSUBCONV_STATIC_RUNTIME=ON") ;;
    --)               shift; extra_args+=("$@"); break ;;
    -*)               echo "未知选项: $1" >&2; usage 2 ;;
    *)                echo "未知参数: $1" >&2; usage 2 ;;
  esac
  shift
done

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_path="$root/$build_dir"

# Termux 的 $PREFIX 不在系统默认搜索路径里，交给 CMake 一起找。
prefix_args=()
if [ -n "$user_prefix" ]; then
  prefix_args+=("-DCMAKE_PREFIX_PATH:STRING=$user_prefix")
elif [ -n "${PREFIX:-}" ]; then
  prefix_args+=("-DCMAKE_PREFIX_PATH:STRING=$PREFIX")
fi

if [ "$clean" = 1 ] && [ -d "$build_path" ]; then
  echo "清理 $build_path"
  rm -rf "$build_path"
fi

echo "== configure =="
cmake -S "$root" -B "$build_path" -G Ninja \
  "-DCMAKE_BUILD_TYPE=$config" \
  ${prefix_args[@]+"${prefix_args[@]}"} \
  ${extra_args[@]+"${extra_args[@]}"}

echo "== build =="
cmake --build "$build_path"

exe="$build_path/subconv"
if [ -x "$exe" ]; then
  echo "== 产物: $exe（$(wc -c < "$exe") 字节）=="
  "$exe" version
fi

if [ "$run_tests" = 1 ]; then
  echo "== 单元测试 =="
  "$build_path/subconv_tests"
fi
