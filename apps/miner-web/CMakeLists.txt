# SPDX-License-Identifier: MIT
# Copyright (c) 2026, Canaan Creative Co., Ltd.
#
# For the licensing terms, see the LICENSE file (MIT License).
#
# NO WARRANTY. The Software is provided "AS IS", without warranty of any kind, express or
# implied, including but not limited to the warranties of merchantability, fitness for a
# particular purpose and non-infringement. In no event shall the authors or copyright
# holders be liable for any claim, damages or other liability arising from, out of or in
# connection with the Software or the use or other dealings in the Software.

cmake_minimum_required(VERSION 3.16)
project(miner_web C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -O2)
# civetweb workers run on a fixed 100 KiB stack; keep request frames well inside it.
add_compile_options(-Werror=frame-larger-than=32768)

set(W ${CMAKE_CURRENT_SOURCE_DIR})
set(MINER ${W}/../miner)

add_executable(miner_web
    app/miner_web_main.c
    web/router.c web/civetweb_glue.c web/telemetry.c web/core_api.c web/webform.c web/logexport.c
    web/upload.c web/webcred.c web/webtoken.c web/webstatus.c web/tlscert.c
    ${MINER}/miner_mgmt/api/webauth.c ${MINER}/miner_mgmt/api/status.c
    ${MINER}/common/pool/pool.c
    ${MINER}/common/pool/pool_config.c
    ${MINER}/common/cfgwr/cfgwr_client.c
    ${MINER}/common/cfgwr/cfgwr_io.c
    ${MINER}/common/sha/sha256.c
    ${MINER}/common/json/jread.c ${MINER}/common/json/jwrite.c ${MINER}/common/ini/ini.c
    ${MINER}/common/ini/ini_file.c)
target_include_directories(miner_web PRIVATE
    ${W}/web ${MINER}/miner_mgmt/api ${MINER}/common/pool
    ${MINER}/common/cfgwr ${MINER}/miner_mgmt/ota
    ${MINER}/common/json ${MINER}/common/ini ${MINER}/common/sha
    ${MINER}/common/proto ${MINER}/common/util ${MINER}/osif
    ${MINER}/hal/board)

option(WEB_PLUGIN "build miner_web with the web plugin of WEB_PLUGIN_DIR" OFF)
set(WEB_PLUGIN_DIR "" CACHE PATH "directory holding web_plugin.cmake")
if(WEB_PLUGIN)
    if(NOT EXISTS "${WEB_PLUGIN_DIR}/web_plugin.cmake")
        message(FATAL_ERROR "WEB_PLUGIN: ${WEB_PLUGIN_DIR}/web_plugin.cmake not found")
    endif()
    include("${WEB_PLUGIN_DIR}/web_plugin.cmake")
else()
    target_sources(miner_web PRIVATE web/web_plugin_stub.c)
endif()

find_path(CIVETWEB_INCLUDE_DIR civetweb.h)
find_library(CIVETWEB_LIBRARY civetweb)
if(NOT CIVETWEB_INCLUDE_DIR OR NOT CIVETWEB_LIBRARY)
    message(FATAL_ERROR "miner_web: civetweb not found")
endif()
target_include_directories(miner_web PRIVATE ${CIVETWEB_INCLUDE_DIR})
target_link_libraries(miner_web PRIVATE ${CIVETWEB_LIBRARY})

# The HTTPS certificate is made on the device (web/tlscert.c).
find_package(OpenSSL REQUIRED)
target_link_libraries(miner_web PRIVATE OpenSSL::Crypto)
