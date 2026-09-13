# ---------------------------------------------------------------------------
# Windows 交叉编译工具链（llvm-mingw，UCRT）
#
# 用法（PATH 里要有 llvm-mingw 的 bin，见 tools/setup-llvm-mingw.sh）：
#
#   tools/setup-llvm-mingw.sh                    # 装到 .cache/llvm-mingw
#   tools/build-windows-deps.sh x86_64-w64-mingw32 \
#       .cache/llvm-mingw .cache/win-deps/x86_64 # 编 OpenSSL + libcurl（静态）
#   export PATH="$PWD/.cache/llvm-mingw/bin:$PATH"
#   cmake -S . -B build-win-x86_64 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=tools/mingw-w64-toolchain.cmake \
#     -DSUBCONV_MINGW_TRIPLE=x86_64-w64-mingw32 \
#     -DSUBCONV_WINDOWS_SYSROOT="$PWD/.cache/win-deps/x86_64"
#   cmake --build build-win-x86_64
#
# 用 llvm-mingw 而不是发行版自带的 mingw-w64，有两个原因：
#   * 一份工具链同时覆盖 x86_64 和 aarch64（Debian/Ubuntu 根本没有 aarch64 的 mingw）
#   * 默认就是 UCRT + libc++，和之前在 MSYS2 里编的 Windows 产物是同一套 ABI
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Windows)

set(SUBCONV_MINGW_TRIPLE "x86_64-w64-mingw32" CACHE STRING "mingw-w64 目标三元组")
set(SUBCONV_WINDOWS_SYSROOT "" CACHE PATH "Windows 依赖（OpenSSL / libcurl）的安装前缀")

if(SUBCONV_MINGW_TRIPLE MATCHES "^aarch64")
  set(CMAKE_SYSTEM_PROCESSOR arm64)
elseif(SUBCONV_MINGW_TRIPLE MATCHES "^i686")
  set(CMAKE_SYSTEM_PROCESSOR x86)
else()
  set(CMAKE_SYSTEM_PROCESSOR amd64)
endif()

set(CMAKE_C_COMPILER "${SUBCONV_MINGW_TRIPLE}-clang")
set(CMAKE_CXX_COMPILER "${SUBCONV_MINGW_TRIPLE}-clang++")
set(CMAKE_RC_COMPILER "${SUBCONV_MINGW_TRIPLE}-windres")
set(CMAKE_AR "${SUBCONV_MINGW_TRIPLE}-ar")
set(CMAKE_RANLIB "${SUBCONV_MINGW_TRIPLE}-ranlib")

if(SUBCONV_WINDOWS_SYSROOT)
  list(PREPEND CMAKE_FIND_ROOT_PATH "${SUBCONV_WINDOWS_SYSROOT}")
  list(APPEND CMAKE_PREFIX_PATH "${SUBCONV_WINDOWS_SYSROOT}")
endif()
# 只到 sysroot 里找库和头文件；找「程序」仍然用宿主机（不限制）
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 静态链 libcurl + libssl + libcrypto 需要这些 Windows 系统库：
#   crypt32  —— OpenSSL 的 CAPI / winstore 后端（CertOpenStore 那一串符号）
#   iphlpapi —— libcurl 的 GetAdaptersAddresses
#   bcrypt   —— libcurl 的 rand 用 BCryptGenRandom
#   ws2_32   —— WinSock（CMakeLists 里也给 subconv_core 挂了一份，但那份排在静态库前面，
#               静态库回指的符号要在这里再兜一次）
# 这批库必须排在链接命令的最后（-l 的解析顺序对静态库是硬要求），所以走
# CMAKE_*_STANDARD_LIBRARIES，而不是 CMAKE_EXE_LINKER_FLAGS（后者排到目标文件前面）。
#
# 而且只能写成 cache 变量：CMake 自带的 Platform/Windows-GNU.cmake 在工具链文件之后又把
# CMAKE_*_STANDARD_LIBRARIES_INIT 覆盖成它自己那份 mingw 默认库，而 CMakeCXXInformation.cmake
# 是「cache 里没有才拿 _INIT 填」（不带 FORCE），所以先在 cache 里放好的值能活下来，
# 普通变量则会被丢掉。默认库那份列表就在上面那个文件里，这里照抄一遍再接上额外的。
set(SUBCONV_WINDOWS_SYSTEM_LIBS
    "-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lws2_32 -lcrypt32 -liphlpapi -lbcrypt"
    CACHE STRING "Windows 目标链接时放在最末尾的系统库")
set(CMAKE_C_STANDARD_LIBRARIES "${SUBCONV_WINDOWS_SYSTEM_LIBS}"
    CACHE STRING "链接所有 C 程序时默认附加的库" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "${SUBCONV_WINDOWS_SYSTEM_LIBS}"
    CACHE STRING "链接所有 C++ 程序时默认附加的库" FORCE)

# 全静态：libc++ / libunwind / winpthread 一并链进去，产物是单个 exe，不依赖旁边的 DLL
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

# 依赖全是静态库，得让 find_package(CURL) 知道这件事：FindCURL.cmake 只有看到
# CURL_USE_STATIC_LIBS 才会给 CURL::libcurl 挂上 CURL_STATICLIB 宏；
# 没有这个宏时 curl 的头文件在 Windows 上把 curl_easy_* 声明成 __declspec(dllimport)，
# 链接静态 libcurl.a 就会报「symbol ... is available in libcurl.a but cannot be used
# because it is not an import library」。
set(CURL_USE_STATIC_LIBS ON CACHE BOOL "libcurl 用静态库（Windows 交叉编译固定静态）")
