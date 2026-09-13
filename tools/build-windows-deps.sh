#!/usr/bin/env bash
# 用 llvm-mingw 交叉编译 Windows 用的静态依赖：OpenSSL + libcurl。
#
#   tools/build-windows-deps.sh <三元组> <llvm-mingw 目录> <安装前缀> [源码目录]
#   tools/build-windows-deps.sh x86_64-w64-mingw32 .cache/llvm-mingw .cache/win-deps/x86_64
#
# 装好之后 <前缀>/lib 下会有 libssl.a / libcrypto.a / libcurl.a，交给
# tools/mingw-w64-toolchain.cmake 的 SUBCONV_WINDOWS_SYSROOT 使用。
#
# 为什么要自己编依赖：Debian/Ubuntu 根本没有给 mingw 预编译的 libcurl / OpenSSL，
# 而 Windows 侧又需要 HTTPS 抓订阅 + --probe-cert 证书探测，所以只能各自交叉编一份。
# 两个库都按静态、最小依赖来配（不带 zlib/brotli/zstd/nghttp2/libssh2…），
# 这样产物是一个自包含的 exe，不用再收集一堆 DLL。
#
# OpenSSL 用 no-asm：llvm-mingw 里没有 GNU as，x86_64 的 perlasm 汇编要额外配 AS=clang，
# 干脆两边都走纯 C —— 这个程序只做几次 TLS 握手，汇编带来的那点性能差异无关紧要。
# 也因此 x86_64 和 aarch64 可以直接用同一个内置目标 mingw64：
#   * 它给的 bn_ops=SIXTY_FOUR_BIT 对 Windows/ARM64 同样成立（Windows 是 LLP64）；
#   * 它加的 -m64 在 aarch64-w64-mingw32-clang 上实测无告警、照样产出 coff-arm64；
#   * 它写的 asm_arch=x86_64 只在启用汇编时才被读取，我们 no-asm 用不到；
#   * 目标名以 mingw 开头，Configure 才会开 winstore（否则 Windows 证书库后端被静默关掉）。
set -euo pipefail

triple="${1:?用法: tools/build-windows-deps.sh <三元组> <llvm-mingw 目录> <安装前缀> [源码目录]}"
mingw="${2:?缺少 llvm-mingw 目录}"
prefix="${3:?缺少安装前缀}"
work="${4:-.cache/win-deps-src}"

openssl_version="${SUBCONV_OPENSSL_VERSION:-3.5.1}"
curl_version="${SUBCONV_CURL_VERSION:-8.15.0}"

case "$triple" in
  x86_64-w64-mingw32|aarch64-w64-mingw32) ssl_target="mingw64" ;;
  *) echo "不支持的三元组：$triple（只支持 x86_64-w64-mingw32 / aarch64-w64-mingw32）" >&2; exit 1 ;;
esac

export PATH="$mingw/bin:$PATH"
export CC="$triple-clang"
export CXX="$triple-clang++"
export AR="$triple-ar"
export RANLIB="$triple-ranlib"
export RC="$triple-windres"
export NM="$triple-nm"

command -v "$CC" >/dev/null || { echo "找不到交叉编译器 $CC —— $mingw/bin 里没有？" >&2; exit 1; }

mkdir -p "$work" "$prefix"
work="$(cd "$work" && pwd)"
prefix="$(cd "$prefix" && pwd)"
jobs="$(nproc 2>/dev/null || echo 4)"

fetch() {  # fetch <url> <目标文件>
  local url="$1" out="$2"
  if [ -s "$out" ]; then return 0; fi
  # 国内网络可以套一层 GitHub 镜像前缀，例如 SUBCONV_GH_MIRROR=https://gh-proxy.com/
  case "$url" in
    https://github.com/*) url="${SUBCONV_GH_MIRROR:-}$url" ;;
  esac
  echo "下载 $url"
  # 先下到 .part 再改名：中断留下的半截文件不会被下次误当成"已下好"
  curl -fSL --retry 5 --retry-delay 3 --retry-all-errors -o "$out.part" "$url"
  mv "$out.part" "$out"
}

# ---------------------------------------------------------------------------
# OpenSSL
# ---------------------------------------------------------------------------
if [ ! -f "$prefix/lib/libssl.a" ] || [ ! -f "$prefix/lib/libcrypto.a" ]; then
  cd "$work"
  if [ ! -d "openssl-$openssl_version" ]; then
    fetch "https://github.com/openssl/openssl/releases/download/openssl-$openssl_version/openssl-$openssl_version.tar.gz" \
      "$work/openssl-$openssl_version.tar.gz"
    tar xf "$work/openssl-$openssl_version.tar.gz"
  fi
  cd "openssl-$openssl_version"

  if [ ! -f Makefile ]; then
    # no-module 不能省：OpenSSL 的 no-shared 只管 libcrypto/libssl 两个动态库，
    # providers/legacy.dll 这种「可动态加载模块」照样会编，而这个 exe 根本用不到它
    # （默认 provider 是静态编进 libcrypto.a 的）。它在 aarch64 上还会直接编失败：
    # 资源对象是按 mingw64 配置里的 shared_rcflag（--target=pe-x86-64）编出来的，
    # 链接时报「machine type x64 conflicts with arm64」。
    perl ./Configure "$ssl_target" \
      no-asm no-shared no-module no-tests no-apps no-docs \
      --prefix="$prefix" --openssldir="$prefix/ssl" --libdir=lib \
      CC="$CC" AR="$AR" RANLIB="$RANLIB" RC="$RC"
  fi
  make -j"$jobs"
  make install_sw
else
  echo "OpenSSL 已存在：$prefix/lib/libssl.a"
fi

# ---------------------------------------------------------------------------
# libcurl（静态，只留 HTTPS/HTTP，去掉所有可选依赖）
# ---------------------------------------------------------------------------
if [ ! -f "$prefix/lib/libcurl.a" ]; then
  cd "$work"
  if [ ! -d "curl-$curl_version" ]; then
    fetch "https://github.com/curl/curl/releases/download/curl-${curl_version//./_}/curl-$curl_version.tar.gz" \
      "$work/curl-$curl_version.tar.gz"
    tar xf "$work/curl-$curl_version.tar.gz"
  fi
  cd "curl-$curl_version"

  # LIBS：configure 用「能不能链上 -lcrypto 里的 HMAC_Update」来判断 OpenSSL 可用，
  # 静态 libcrypto 自己还需要 ws2_32/gdi32/crypt32，不先给这三个就会误判成"找不到 OpenSSL"。
  if [ ! -f Makefile ]; then
    LIBS="-lws2_32 -lgdi32 -lcrypt32" ./configure \
      --host="$triple" --prefix="$prefix" \
      --with-openssl="$prefix" \
      --disable-shared --enable-static \
      --without-zlib --without-brotli --without-zstd --without-libpsl --without-libidn2 \
      --without-nghttp2 --without-libssh2 \
      --disable-ldap --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
      --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
      --disable-manual
  fi
  make -j"$jobs"
  make install
else
  echo "libcurl 已存在：$prefix/lib/libcurl.a"
fi

echo
echo "== $triple 依赖就绪（$prefix）=="
ls -l "$prefix"/lib/*.a
