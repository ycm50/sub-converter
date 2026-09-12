cmake_minimum_required(VERSION 3.20)
project(subconv VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 基本设置
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SUBCONV_BUILD_TESTS "构建单元测试" ON)
option(SUBCONV_USE_CURL   "启用 libcurl 订阅抓取" ON)
option(SUBCONV_USE_YAML   "启用 yaml-cpp 解析 Clash 订阅" ON)

# MSYS2 UCRT64 前缀提示（未在 MSYS2 shell 内运行时也能找到依赖）
if(WIN32)
  if(DEFINED ENV{MSYS2_PREFIX} AND EXISTS "$ENV{MSYS2_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{MSYS2_PREFIX}")
  elseif(EXISTS "A:/msys64/ucrt64")
    list(APPEND CMAKE_PREFIX_PATH "A:/msys64/ucrt64")
  endif()
endif()

if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 核心静态库
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Web UI 资源：data/web/index.html 内嵌进二进制，避免运行时找资源路径
# ---------------------------------------------------------------------------
set(SUBCONV_WEB_UI_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/data/web/index.html")
if(NOT EXISTS "${SUBCONV_WEB_UI_SOURCE}")
  message(FATAL_ERROR "缺少 Web UI 资源: ${SUBCONV_WEB_UI_SOURCE}")
endif()
# 改 HTML 也要重新 configure，否则内嵌的内容不会更新
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SUBCONV_WEB_UI_SOURCE}")
file(READ "${SUBCONV_WEB_UI_SOURCE}" SUBCONV_WEB_UI_HTML)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/web_ui.hpp"
"// 本文件由 CMake 从 data/web/index.html 生成 —— 请勿手工编辑。
#pragma once

namespace subconv::server {

inline constexpr const char* kWebUiHtml = R\"SUBCONVHTML(
${SUBCONV_WEB_UI_HTML}
)SUBCONVHTML\";

}  // namespace subconv::server
")

# ---------------------------------------------------------------------------
# 核心静态库
# ---------------------------------------------------------------------------
add_library(subconv_core STATIC
  src/core/types.cpp
  src/core/fsutil.cpp
  src/core/console.cpp
  src/codec/base64.cpp
  src/codec/url.cpp
  src/parse/ss.cpp
  src/parse/ssr.cpp
  src/parse/vmess.cpp
  src/parse/vless.cpp
  src/parse/trojan.cpp
  src/parse/hysteria.cpp
  src/parse/tuic.cpp
  src/parse/snell.cpp
  src/parse/uri_common.cpp
  src/parse/clash_yaml.cpp
  src/parse/parse.cpp
  src/fetch/http.cpp
  src/fetch/load.cpp
  src/emit/yaml.cpp
  src/emit/clash.cpp
  src/emit/xray.cpp
  src/emit/singbox.cpp
  src/emit/sharelink.cpp
  src/emit/rulesets.cpp
  src/emit/dns.cpp
  src/emit/emit.cpp
  src/server/convert.cpp
  src/server/request.cpp
  src/server/http.cpp
)

target_include_directories(subconv_core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>"
)

target_include_directories(subconv_core PRIVATE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)

# --- WinSock（HTTP 服务）-----------------------------------------------------
if(WIN32)
  target_link_libraries(subconv_core PUBLIC ws2_32)
endif()

target_compile_definitions(subconv_core PUBLIC
  SUBCONV_VERSION="${PROJECT_VERSION}"
)

# --- libcurl ---------------------------------------------------------------
if(SUBCONV_USE_CURL)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    target_link_libraries(subconv_core PUBLIC CURL::libcurl)
    target_compile_definitions(subconv_core PUBLIC SUBCONV_HAVE_CURL=1)
    message(STATUS "libcurl: ${CURL_VERSION_STRING} -> 启用订阅抓取")
  else()
    message(STATUS "libcurl: 未找到 -> 订阅抓取暂不可用（仅支持本地文件）")
  endif()
endif()

# --- yaml-cpp --------------------------------------------------------------
if(SUBCONV_USE_YAML)
  find_package(yaml-cpp QUIET)
  if(TARGET yaml-cpp::yaml-cpp)
    set(SUBCONV_YAML_TARGET yaml-cpp::yaml-cpp)
  elseif(TARGET yaml-cpp)
    set(SUBCONV_YAML_TARGET yaml-cpp)
  endif()
  if(SUBCONV_YAML_TARGET)
    target_link_libraries(subconv_core PUBLIC ${SUBCONV_YAML_TARGET})
    target_compile_definitions(subconv_core PUBLIC SUBCONV_HAVE_YAML=1)
    message(STATUS "yaml-cpp: 启用 Clash 订阅解析")
  else()
    message(STATUS "yaml-cpp: 未找到 -> 暂不支持解析 Clash 订阅")
  endif()
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(subconv src/cli/main.cpp)
target_link_libraries(subconv PRIVATE subconv_core)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(SUBCONV_BUILD_TESTS)
  enable_testing()
  add_executable(subconv_tests tests/test_main.cpp)
  target_link_libraries(subconv_tests PRIVATE subconv_core)
  add_test(NAME subconv_tests COMMAND subconv_tests)
endif()

# ---------------------------------------------------------------------------
# 安装
# ---------------------------------------------------------------------------
install(TARGETS subconv RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/subconv/data OPTIONAL)

message(STATUS "subconv ${PROJECT_VERSION} | C++${CMAKE_CXX_STANDARD} | ${CMAKE_BUILD_TYPE} | ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
