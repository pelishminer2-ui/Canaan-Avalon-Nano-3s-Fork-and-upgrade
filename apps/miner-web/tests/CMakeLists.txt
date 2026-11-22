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
project(miner_web_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -O2)
add_compile_options(-UNDEBUG)
set(TST_SKIP 77)
add_compile_definitions(TST_SKIP=${TST_SKIP})

set(WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../web)
set(CTL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../miner/miner_mgmt)
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../miner/common)
set(POOL_DIR ${COMMON_DIR}/pool)
set(CFGWR_DIR ${COMMON_DIR}/cfgwr)

set(WEB_STACK_SRCS stub_civetweb.c
    ${CFGWR_DIR}/cfgwr_client.c ${CFGWR_DIR}/cfgwr_io.c
    ${WEB_DIR}/upload.c ${WEB_DIR}/router.c ${WEB_DIR}/web_plugin_stub.c
    ${WEB_DIR}/webform.c ${WEB_DIR}/logexport.c ${WEB_DIR}/webcred.c webtoken_paths.c
    ${WEB_DIR}/webstatus.c ${WEB_DIR}/core_api.c
    ${POOL_DIR}/pool.c ${POOL_DIR}/pool_config.c
    ${CTL_DIR}/api/webauth.c ${CTL_DIR}/api/status.c
    ${COMMON_DIR}/json/jread.c ${COMMON_DIR}/json/jwrite.c ${COMMON_DIR}/ini/ini.c
    ${COMMON_DIR}/ini/ini_file.c ${COMMON_DIR}/sha/sha256.c)
set(WEB_STACK_INCS
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${WEB_DIR} ${CTL_DIR}/api ${CTL_DIR}/ota ${CFGWR_DIR} ${POOL_DIR}
    ${COMMON_DIR}/json ${COMMON_DIR}/ini ${COMMON_DIR}/sha ${COMMON_DIR}/proto
    ${CMAKE_CURRENT_SOURCE_DIR}/../../miner/hal/board)

find_package(OpenSSL REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../miner/osif)
include_directories(${COMMON_DIR}/util)

enable_testing()

add_executable(test_router test_router.c
    ${WEB_DIR}/router.c ${WEB_DIR}/webform.c ${WEB_DIR}/webcred.c ${WEB_DIR}/web_plugin_stub.c
    ${WEB_DIR}/webstatus.c
    ${POOL_DIR}/pool.c
    ${POOL_DIR}/pool_config.c
    ${CTL_DIR}/api/webauth.c ${CTL_DIR}/api/status.c
    ${COMMON_DIR}/json/jread.c ${COMMON_DIR}/json/jwrite.c ${COMMON_DIR}/ini/ini.c
    ${COMMON_DIR}/ini/ini_file.c ${COMMON_DIR}/sha/sha256.c)
target_include_directories(test_router PRIVATE
    ${WEB_DIR} ${CTL_DIR}/api ${POOL_DIR} ${COMMON_DIR}/json ${COMMON_DIR}/ini
    ${COMMON_DIR}/sha ${CFGWR_DIR} ${COMMON_DIR}/proto)
target_link_libraries(test_router PRIVATE pthread OpenSSL::Crypto)
add_test(NAME router COMMAND test_router)

add_executable(test_logexport test_logexport.c ${COMMON_DIR}/sha/sha256.c)
target_include_directories(test_logexport PRIVATE ${WEB_DIR} ${COMMON_DIR}/sha ${COMMON_DIR}/proto)
add_test(NAME logexport COMMAND test_logexport)

add_executable(test_webform test_webform.c ${WEB_DIR}/webform.c ${POOL_DIR}/pool.c)
target_include_directories(test_webform PRIVATE ${WEB_DIR} ${POOL_DIR})
add_test(NAME webform COMMAND test_webform)

add_executable(test_poolsave test_poolsave.c
    ${WEB_DIR}/webform.c ${POOL_DIR}/pool.c ${POOL_DIR}/pool_config.c
    ${COMMON_DIR}/json/jread.c ${COMMON_DIR}/json/jwrite.c)
target_include_directories(test_poolsave PRIVATE
    ${WEB_DIR} ${POOL_DIR} ${COMMON_DIR}/json ${COMMON_DIR}/proto)
add_test(NAME poolsave COMMAND test_poolsave)

set(FIXTURES_DIR ${CTL_DIR}/../tests/harness/fixtures)
add_executable(test_telemetry test_telemetry.c ${CTL_DIR}/api/status.c
    ${COMMON_DIR}/json/jread.c ${COMMON_DIR}/json/jwrite.c)
target_include_directories(test_telemetry PRIVATE
    ${WEB_DIR} ${CTL_DIR}/api ${COMMON_DIR}/json ${COMMON_DIR}/proto)
target_compile_definitions(test_telemetry PRIVATE API_FIXTURES_DIR="${FIXTURES_DIR}")
add_test(NAME telemetry COMMAND test_telemetry)
set_tests_properties(telemetry PROPERTIES TIMEOUT 30)

add_executable(test_core_api test_core_api.c ${COMMON_DIR}/json/jwrite.c)
target_include_directories(test_core_api PRIVATE ${WEB_DIR} ${COMMON_DIR}/json ${COMMON_DIR}/proto)
add_test(NAME core_api COMMAND test_core_api)

set(PROD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../product/k230/miner)
set(GEN_BOARDCONF ${CTL_DIR}/../tools/gen_boardconf.py)
set(X3_FIXTURES ${CTL_DIR}/../tests/fixtures)
foreach(BOARD_INI nano3s:${PROD_DIR}/avalon-home/nano3s/conf/product/boardconf.ini
    x3:${X3_FIXTURES}/boardconf_x3.ini)
    string(REPLACE ":" ";" BOARD_INI ${BOARD_INI})
    list(GET BOARD_INI 0 BOARD)
    list(GET BOARD_INI 1 BOARD_CONF)
    set(BOARD_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen_${BOARD})
    add_custom_command(OUTPUT ${BOARD_GEN}/boardconf.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BOARD_GEN}
        COMMAND python3 ${GEN_BOARDCONF} ${BOARD_CONF} ${BOARD_GEN}/boardconf.h
        DEPENDS ${BOARD_CONF} ${GEN_BOARDCONF})
    add_executable(test_reply_cap_${BOARD} test_reply_cap.c ${BOARD_GEN}/boardconf.h)
    target_include_directories(test_reply_cap_${BOARD} PRIVATE
        ${WEB_DIR} ${CTL_DIR}/api ${CTL_DIR}/../miner_core/api ${COMMON_DIR}/proto
        ${CTL_DIR}/../hal/board ${X3_FIXTURES} ${BOARD_GEN})
    target_compile_definitions(test_reply_cap_${BOARD} PRIVATE
        CORE_MINER_VERSION="0" CORE_MINER_SEMVER="0" MM_MINER_VERSION="0"
        ASIC_MINER_VERSION="0")
    add_test(NAME reply_cap_${BOARD} COMMAND test_reply_cap_${BOARD})
endforeach()

add_executable(test_upload test_upload.c ${WEB_STACK_SRCS})
target_include_directories(test_upload PRIVATE ${WEB_STACK_INCS})
target_link_libraries(test_upload PRIVATE pthread OpenSSL::Crypto)
add_test(NAME upload COMMAND test_upload)

add_executable(test_upload_handler test_upload_handler.c ${WEB_STACK_SRCS})
target_include_directories(test_upload_handler PRIVATE ${WEB_STACK_INCS})
target_link_libraries(test_upload_handler PRIVATE pthread OpenSSL::Crypto)
add_test(NAME upload_handler COMMAND test_upload_handler)

set(WALLPAPER_SRCS ${WEB_STACK_SRCS})
list(REMOVE_ITEM WALLPAPER_SRCS ${WEB_DIR}/core_api.c)
add_executable(test_wallpaper test_wallpaper.c ${WALLPAPER_SRCS})
target_include_directories(test_wallpaper PRIVATE ${WEB_STACK_INCS})
target_link_libraries(test_wallpaper PRIVATE pthread OpenSSL::Crypto)
add_test(NAME wallpaper COMMAND test_wallpaper)

set(WEB_HANDLER_SRCS ${WEB_STACK_SRCS})
list(REMOVE_ITEM WEB_HANDLER_SRCS ${WEB_DIR}/logexport.c)
add_executable(test_web_handler test_web_handler.c ${WEB_HANDLER_SRCS})
target_include_directories(test_web_handler PRIVATE ${WEB_STACK_INCS})
target_link_libraries(test_web_handler PRIVATE pthread OpenSSL::Crypto)
add_test(NAME web_handler COMMAND test_web_handler)

foreach(t test_upload test_upload_handler test_wallpaper test_web_handler)
    target_compile_definitions(${t} PRIVATE
        TST_TOKEN_KEY="${CMAKE_CURRENT_BINARY_DIR}/${t}.tokenkey")
endforeach()

add_executable(test_webtoken test_webtoken.c ${COMMON_DIR}/json/jread.c ${COMMON_DIR}/json/jwrite.c)
target_include_directories(test_webtoken PRIVATE ${WEB_DIR} ${CTL_DIR}/api ${COMMON_DIR}/json
    ${COMMON_DIR}/proto)
target_link_libraries(test_webtoken PRIVATE pthread OpenSSL::Crypto)
add_test(NAME webtoken COMMAND test_webtoken)

add_executable(test_webcred test_webcred.c ${COMMON_DIR}/ini/ini_file.c ${COMMON_DIR}/ini/ini.c)
target_include_directories(test_webcred PRIVATE ${WEB_DIR} ${CTL_DIR}/api
    ${COMMON_DIR}/proto ${COMMON_DIR}/ini)
target_link_libraries(test_webcred PRIVATE pthread)
add_test(NAME webcred COMMAND test_webcred)

add_executable(test_tlscert test_tlscert.c ${WEB_DIR}/tlscert.c)
target_include_directories(test_tlscert PRIVATE ${WEB_DIR})
target_link_libraries(test_tlscert PRIVATE OpenSSL::Crypto)
add_test(NAME tlscert COMMAND test_tlscert)

add_executable(test_web_logfmt test_web_logfmt.c)
target_include_directories(test_web_logfmt PRIVATE ${WEB_DIR})
add_test(NAME web_logfmt COMMAND test_web_logfmt)

set(WEB_INIT ${CMAKE_CURRENT_SOURCE_DIR}/../services/miner-web.init)
add_test(NAME init COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_init.sh ${WEB_INIT})
# The board runs init scripts with the busybox shell.
find_program(BUSYBOX_EXECUTABLE busybox)
if(BUSYBOX_EXECUTABLE)
    add_test(NAME init_busybox COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_init.sh
        ${WEB_INIT} "${BUSYBOX_EXECUTABLE} sh")
else()
    add_test(NAME init_busybox COMMAND sh -c "echo 'SKIP: no busybox'; exit ${TST_SKIP}")
endif()

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME pages COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_pages.js)
else()
    add_test(NAME pages COMMAND sh -c "echo 'SKIP: no node'; exit ${TST_SKIP}")
endif()

get_property(ALL_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${ALL_TESTS} PROPERTIES SKIP_RETURN_CODE ${TST_SKIP})
