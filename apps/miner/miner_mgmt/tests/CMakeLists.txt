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
project(miner_mgmt_tests C)
set(CTRL_CMDS_C ${CMAKE_CURRENT_LIST_DIR}/../../common/proto/ctrl_cmds.c)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../common/proto)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../osif)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../common/log)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../common/util)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror=format -Wno-error=format-truncation -O2)
add_compile_options(-UNDEBUG)
set(TST_SKIP 77)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/MinerProtoVer.cmake)
add_compile_definitions(FW_TYPE="Debug" MINER_PROTO_VER="${MINER_PROTO_VER}" TST_SKIP=${TST_SKIP})
add_compile_definitions(CORE_MINER_VERSION="00000000_0000_unknown"
    CORE_MINER_SEMVER="0.0.0"
    MM_MINER_VERSION="00000000_0000_unknown"
    ASIC_MINER_VERSION="00000000_0000_unknown")

set(CTL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMMON_DIR ${CTL_DIR}/../common)
include_directories(${COMMON_DIR}/ini)
include_directories(${CTL_DIR}/../miner_core/stratum)
include_directories(${COMMON_DIR}/pool ${COMMON_DIR}/cfgwr)
include_directories(${CTL_DIR}/net)
set(FIXTURES_DIR ${CTL_DIR}/../tests/harness/fixtures)

enable_testing()

find_package(OpenSSL REQUIRED)
include(${CTL_DIR}/../cmake/AupPubkey.cmake)
add_executable(test_mm_model test_mm_model.c)
target_include_directories(test_mm_model PRIVATE ${CTL_DIR}/../app
    ${CTL_DIR}/../hal/platform/linux)
add_test(NAME mm_model COMMAND test_mm_model)

set(BOARDCONF_INI
    ${CTL_DIR}/../../../product/k230/miner/avalon-home/nano3s/conf/product/boardconf.ini)
set(BOARDCONF_H ${CMAKE_CURRENT_BINARY_DIR}/gen/boardconf.h)
set(BOARDCONF_TABLES ${CTL_DIR}/../hal/board/board_tables.c)
include_directories(${CTL_DIR}/../hal/board ${CTL_DIR}/../tests/fixtures)
add_custom_command(OUTPUT ${BOARDCONF_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
    COMMAND python3 ${CTL_DIR}/../tools/gen_boardconf.py ${BOARDCONF_INI} ${BOARDCONF_H}
    DEPENDS ${BOARDCONF_INI} ${CTL_DIR}/../tools/gen_boardconf.py
    COMMENT "gen boardconf.h from ${BOARDCONF_INI}")
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

set(BOARDCONF_X3_INI ${CTL_DIR}/../tests/fixtures/boardconf_x3.ini)
set(BOARDCONF_X3_H ${CMAKE_CURRENT_BINARY_DIR}/gen_x3/boardconf.h)
set(BOARDCONF_X3_TABLES ${CTL_DIR}/../hal/board/board_tables.c)
add_custom_command(OUTPUT ${BOARDCONF_X3_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen_x3
    COMMAND python3 ${CTL_DIR}/../tools/gen_boardconf.py ${BOARDCONF_X3_INI} ${BOARDCONF_X3_H}
    DEPENDS ${BOARDCONF_X3_INI} ${CTL_DIR}/../tools/gen_boardconf.py
    COMMENT "gen boardconf.h from ${BOARDCONF_X3_INI}")
set(GEN_X3_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen_x3)

include(${CTL_DIR}/../cmake/MinerTestBoards.cmake)
miner_board_register(nano3s ${BOARDCONF_H} ${BOARDCONF_TABLES} ${GEN_DIR})
miner_board_register(x3 ${BOARDCONF_X3_H} ${BOARDCONF_X3_TABLES} ${GEN_X3_DIR})

set(MINER_TEST_BOARDS nano3s x3)

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_cfgwr_${BOARD} test_cfgwr.c
        ${COMMON_DIR}/cfgwr/cfgwr_io.c
        ${COMMON_DIR}/pool/pool.c
        ${COMMON_DIR}/pool/pool_config.c
        ${COMMON_DIR}/json/jread.c ${COMMON_DIR}/json/jwrite.c
        ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
        ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
        ${CTL_DIR}/../miner_core/api/mc_api_bootby.c
        ${CTL_DIR}/../osif/linux/osif_thread_linux.c
        ${CTL_DIR}/../osif/linux/osif_log_linux.c)
    add_dependencies(test_cfgwr_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_cfgwr_${BOARD} PRIVATE ${CTL_DIR}/../app
        ${CTL_DIR}/../osif ${CTL_DIR}/api ${COMMON_DIR}/json ${CTL_DIR}/../miner_core/api
        ${CTL_DIR}/../hal/board ${BCDIR})
    target_link_libraries(test_cfgwr_${BOARD} PRIVATE pthread)
    add_test(NAME cfgwr_${BOARD} COMMAND test_cfgwr_${BOARD})
    set_tests_properties(cfgwr_${BOARD} PROPERTIES TIMEOUT 30)
endforeach()

add_executable(test_webauth test_webauth.c ${CTL_DIR}/api/webauth.c ${COMMON_DIR}/ini/ini.c)
target_include_directories(test_webauth PRIVATE ${CTL_DIR}/api ${COMMON_DIR}/ini)
target_compile_definitions(test_webauth PRIVATE API_FIXTURES_DIR="${FIXTURES_DIR}")
add_test(NAME webauth COMMAND test_webauth)

add_executable(test_status test_status.c ${CTL_DIR}/api/status.c ${COMMON_DIR}/json/jwrite.c)
target_include_directories(test_status PRIVATE ${CTL_DIR}/api ${COMMON_DIR}/json)
add_test(NAME status COMMAND test_status)

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_cali_${BOARD} test_cali.c ${CTL_DIR}/cali/cali.c
        ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c ${COMMON_DIR}/ini/ini_cache.c
        ${COMMON_DIR}/util/cfg_u32.c)
    add_dependencies(test_cali_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_cali_${BOARD} PRIVATE ${CTL_DIR}/cali
        ${COMMON_DIR}/ini ${CTL_DIR}/../hal/board ${CTL_DIR}/../tests/fixtures
        ${BCDIR})
    target_compile_definitions(test_cali_${BOARD} PRIVATE
        CALI_TEST_PREFIX="${CMAKE_CURRENT_BINARY_DIR}/cali_${BOARD}")
    target_link_options(test_cali_${BOARD} PRIVATE -Wl,--wrap=ini_load)
    target_link_libraries(test_cali_${BOARD} PRIVATE pthread)
    add_test(NAME cali_${BOARD} COMMAND test_cali_${BOARD})
endforeach()

add_executable(test_framework test_framework.c ${CTL_DIR}/framework.c)
target_include_directories(test_framework PRIVATE ${CTL_DIR})
add_test(NAME framework COMMAND test_framework)

add_executable(test_ui_dispatch test_ui_dispatch.c ${CTL_DIR}/ui/ui_dispatch.c)
target_include_directories(test_ui_dispatch PRIVATE ${CTL_DIR}/ui)
add_test(NAME ui_dispatch COMMAND test_ui_dispatch)

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_net_discover_${BOARD} test_net_discover.c
        ${CTL_DIR}/net/net_discover.c ${CTL_DIR}/../osif/linux/osif_log_linux.c)
    add_dependencies(test_net_discover_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_net_discover_${BOARD} PRIVATE ${CTL_DIR}/net
        ${CTL_DIR}/../osif ${CTL_DIR}/../hal/board ${BCDIR})
    target_link_libraries(test_net_discover_${BOARD} pthread)
    add_test(NAME net_discover_${BOARD} COMMAND test_net_discover_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_fan_pid_${BOARD} test_fan_pid.c ${CTL_DIR}/ctrl/fan_pid.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_fan_pid_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_fan_pid_${BOARD} PRIVATE ${CTL_DIR}/ctrl ${BCDIR}
        ${CTL_DIR}/../hal/board)
    target_link_libraries(test_fan_pid_${BOARD} PRIVATE m)
    add_test(NAME fan_pid_${BOARD} COMMAND test_fan_pid_${BOARD})

    add_executable(test_autotune_${BOARD} test_autotune.c ${CTL_DIR}/ctrl/autotune.c)
    add_dependencies(test_autotune_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_autotune_${BOARD} PRIVATE ${CTL_DIR}/ctrl ${BCDIR}
        ${CTL_DIR}/../hal/board ${CTL_DIR}/../app)
    add_test(NAME autotune_${BOARD} COMMAND test_autotune_${BOARD})
endforeach()

add_executable(test_pout_reg test_pout_reg.c ${CTL_DIR}/ctrl/pout_reg.c)
target_include_directories(test_pout_reg PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME pout_reg COMMAND test_pout_reg)

add_executable(test_fan_power_loop test_fan_power_loop.c ${CTL_DIR}/ctrl/fan_pid.c
    ${CTL_DIR}/ctrl/pout_reg.c)
add_dependencies(test_fan_power_loop gen_boardconf_nano3s)
target_include_directories(test_fan_power_loop PRIVATE ${CTL_DIR}/ctrl ${GEN_DIR}
    ${CTL_DIR}/../hal/board)
target_link_libraries(test_fan_power_loop PRIVATE m)
add_test(NAME fan_power_loop COMMAND test_fan_power_loop)

add_executable(test_thermal test_thermal.c ${CTL_DIR}/ctrl/thermal.c)
add_dependencies(test_thermal gen_boardconf_nano3s)
target_include_directories(test_thermal PRIVATE ${CTL_DIR}/ctrl ${GEN_DIR})
add_test(NAME thermal COMMAND test_thermal)

add_executable(test_fan_health test_fan_health.c ${CTL_DIR}/ctrl/fan_health.c)
target_include_directories(test_fan_health PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME fan_health COMMAND test_fan_health)

add_executable(test_enum_hold test_enum_hold.c ${CTL_DIR}/ctrl/enum_hold.c)
target_include_directories(test_enum_hold PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME enum_hold COMMAND test_enum_hold)

add_executable(test_softonoff_sched test_softonoff_sched.c ${CTL_DIR}/ctrl/softonoff_sched.c)
target_include_directories(test_softonoff_sched PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME softonoff_sched COMMAND test_softonoff_sched)

add_executable(test_net_select test_net_select.c ${CTL_DIR}/ctrl/net_select.c)
target_include_directories(test_net_select PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME net_select COMMAND test_net_select)

add_executable(test_wdt_flags test_wdt_flags.c ${CTL_DIR}/ctrl/wdt_flags.c)
target_include_directories(test_wdt_flags PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME wdt_flags COMMAND test_wdt_flags)

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_net_repair_${BOARD} test_net_repair.c
        ${CTL_DIR}/net/net_state.c ${BCT})
    add_dependencies(test_net_repair_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_net_repair_${BOARD} PRIVATE
        ${CTL_DIR}/net ${BCDIR} ${CTL_DIR}/../hal/board)
    add_test(NAME net_repair_${BOARD} COMMAND test_net_repair_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_net_state_${BOARD} test_net_state.c
        ${CTL_DIR}/net/net_state.c ${BCT})
    add_dependencies(test_net_state_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_net_state_${BOARD} PRIVATE
        ${CTL_DIR}/net ${BCDIR} ${CTL_DIR}/../hal/board)
    target_link_options(test_net_state_${BOARD} PRIVATE -Wl,--wrap=ioctl)
    add_test(NAME net_state_${BOARD} COMMAND test_net_state_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_softonoff_${BOARD} test_softonoff.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c ${CTL_DIR}/net/net_state.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_softonoff_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_softonoff_${BOARD} PRIVATE ${CTL_DIR}/ctrl ${BCDIR}
        ${CTL_DIR}/../hal/board)
    add_test(NAME softonoff_${BOARD} COMMAND test_softonoff_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_fault_report_${BOARD} test_fault_report.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_fault_report_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_fault_report_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${BCDIR}
        ${CTL_DIR}/../hal/board)
    add_test(NAME fault_report_${BOARD} COMMAND test_fault_report_${BOARD})
endforeach()

add_executable(test_aging test_aging.c ${CTL_DIR}/ctrl/aging.c)
target_include_directories(test_aging PRIVATE ${CTL_DIR}/ctrl)
add_test(NAME aging COMMAND test_aging)

add_executable(test_led_fx test_led_fx.c ${CTL_DIR}/ctrl/led_fx.c)
target_include_directories(test_led_fx PRIVATE ${CTL_DIR}/ctrl)
target_compile_definitions(test_led_fx PRIVATE LED_PIXEL_COUNT=9)
add_test(NAME led_fx COMMAND test_led_fx)

add_executable(test_btn_decode test_btn_decode.c ${CTL_DIR}/input/btn_decode.c)
target_include_directories(test_btn_decode PRIVATE ${CTL_DIR}/input)
add_test(NAME btn_decode COMMAND test_btn_decode)

add_executable(test_btn_input test_btn_input.c
    ${CTRL_CMDS_C} ${BOARDCONF_TABLES})
add_dependencies(test_btn_input gen_boardconf_nano3s)
target_include_directories(test_btn_input PRIVATE ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board
    ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif ${GEN_DIR})
add_test(NAME btn_input COMMAND test_btn_input)

add_executable(test_mm_ui_cfg test_mm_ui_cfg.c
    ${CTL_DIR}/../app/mm_ui_cfg.c ${COMMON_DIR}/util/cfg_u32.c
    ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
    ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
    ${CTL_DIR}/../miner_core/api/mc_api_bootby.c)
target_include_directories(test_mm_ui_cfg PRIVATE ${CTL_DIR}/../app ${CTL_DIR}/ui ${COMMON_DIR}/ini
    ${CTL_DIR}/../osif ${CTL_DIR}/../miner_core/api)
target_link_libraries(test_mm_ui_cfg PRIVATE pthread)
add_test(NAME mm_ui_cfg COMMAND test_mm_ui_cfg)

add_executable(test_mm_display test_mm_display.c
    ${CTL_DIR}/../app/mm_display.c ${CTL_DIR}/../app/mm_ui_cfg.c ${COMMON_DIR}/util/cfg_u32.c
    ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
    ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
    ${CTL_DIR}/../miner_core/api/mc_api_bootby.c)
target_include_directories(test_mm_display PRIVATE ${CTL_DIR}/../app ${CTL_DIR}/ui ${COMMON_DIR}/ini
    ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif ${CTL_DIR}/api
    ${CTL_DIR}/../miner_core/api)
target_compile_definitions(test_mm_display PRIVATE SUPPORT_LCD=1)
target_link_libraries(test_mm_display PRIVATE pthread)
add_test(NAME mm_display COMMAND test_mm_display)

add_executable(test_ui_loop test_ui_loop.c stub_ui_backlight.c ${CTL_DIR}/ui/ui_dispatch.c)
target_include_directories(test_ui_loop PRIVATE ${CTL_DIR}/ui ${CTL_DIR}/../hal/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_compile_definitions(test_ui_loop PRIVATE SUPPORT_LCD=1)
target_link_libraries(test_ui_loop PRIVATE pthread)
target_link_options(test_ui_loop PRIVATE -Wl,--wrap=calloc)
add_test(NAME ui_loop COMMAND test_ui_loop)

add_library(test_mm_ui_glue_compile OBJECT ${CTL_DIR}/../app/mm_ui_glue.c
    ${CTL_DIR}/../app/mm_factory.c)
add_dependencies(test_mm_ui_glue_compile gen_boardconf_nano3s)
target_include_directories(test_mm_ui_glue_compile PRIVATE
    ${CTL_DIR}/../app ${CTL_DIR}/ui ${CTL_DIR}/input ${CTL_DIR}/cali ${CTL_DIR}/api ${CTL_DIR}/ctrl
    ${CTL_DIR}/net ${CTL_DIR}/../hal ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif
    ${CTL_DIR}/../miner_core/api ${CTL_DIR}/../miner_core/stratum
    ${COMMON_DIR}/ini ${COMMON_DIR}/json ${GEN_DIR})
target_compile_definitions(test_mm_ui_glue_compile PRIVATE SUPPORT_LCD=1 SUPPORT_BLE=1)

foreach(VARIANT mm_ui_provision mm_ui_provision_noble)
    if(VARIANT STREQUAL mm_ui_provision)
        set(BOARD nano3s)
    else()
        set(BOARD x3)
    endif()
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_${VARIANT} test_mm_ui_provision.c
        ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
        ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
        ${CTL_DIR}/../miner_core/api/mc_api_bootby.c)
    add_dependencies(test_${VARIANT} gen_boardconf_${BOARD})
    target_include_directories(test_${VARIANT} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR}/ui ${CTL_DIR}/input ${CTL_DIR}/cali ${CTL_DIR}/api
        ${CTL_DIR}/ctrl ${CTL_DIR}/net ${CTL_DIR}/../hal ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_${VARIANT} PRIVATE SUPPORT_LCD=1)
    if(VARIANT STREQUAL mm_ui_provision)
        set(SEED_DIR ${CTL_DIR}/../../../product/k230/miner/avalon-home/nano3s/files/usrcon)
        target_compile_definitions(test_${VARIANT} PRIVATE SUPPORT_BLE=1
            FACTORY_SEED_DIR="${SEED_DIR}")
    else()
        set(SEED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/x3/usrcon)
        target_compile_definitions(test_${VARIANT} PRIVATE
            FACTORY_SEED_DIR="${SEED_DIR}")
    endif()
    target_compile_options(test_${VARIANT} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(test_${VARIANT} PRIVATE -Wl,--gc-sections)
    target_link_libraries(test_${VARIANT} PRIVATE pthread)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${VARIANT})
    add_test(NAME ${VARIANT} COMMAND test_${VARIANT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${VARIANT})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    if(BOARD STREQUAL nano3s)
        set(PRODUCT_FILES ${CTL_DIR}/../../../product/k230/miner/avalon-home/nano3s/files)
    else()
        set(PRODUCT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/${BOARD})
    endif()
    add_executable(test_cfg_defaults_${BOARD} test_cfg_defaults.c ${COMMON_DIR}/ini/ini.c)
    add_dependencies(test_cfg_defaults_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_cfg_defaults_${BOARD} PRIVATE ${BCDIR} ${CTL_DIR}/../hal/board
        ${CTL_DIR}/../app ${CTL_DIR}/cali ${CTL_DIR}/ui ${CTL_DIR}/../miner_core/api)
    target_compile_definitions(test_cfg_defaults_${BOARD} PRIVATE
        FACTORY_SEED_DIR="${PRODUCT_FILES}/usrcon")
    add_test(NAME cfg_defaults_${BOARD} COMMAND test_cfg_defaults_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_led_${BOARD} test_mm_led.c
        ${CTL_DIR}/../app/mm_led.c ${CTL_DIR}/../app/ctrl_args.c ${COMMON_DIR}/util/cfg_u32.c
        ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
        ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
        ${CTL_DIR}/../miner_core/api/mc_api_bootby.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_led_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_led_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR}/ctrl ${CTL_DIR}/cali ${CTL_DIR}/api
        ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif
        ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api
        ${CTL_DIR}/../hal/board ${BCDIR})
    target_link_libraries(test_mm_led_${BOARD} PRIVATE pthread)
    target_link_options(test_mm_led_${BOARD} PRIVATE -Wl,--wrap=mm_cfgstore_set_n
        -Wl,--wrap=mm_cfgstore_flush)
    target_compile_definitions(test_mm_led_${BOARD} PRIVATE SUPPORT_LCD)
    add_test(NAME mm_led_${BOARD} COMMAND test_mm_led_${BOARD})
endforeach()

add_executable(test_mm_led_write test_mm_led_write.c ${CTL_DIR}/ctrl/led_fx.c)
add_dependencies(test_mm_led_write gen_boardconf_nano3s)
target_include_directories(test_mm_led_write PRIVATE
    ${CTL_DIR}/../app ${CTL_DIR}/ctrl ${CTL_DIR}/cali ${CTL_DIR}/api
    ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif
    ${CTL_DIR}/../miner_core/api ${GEN_DIR})
target_compile_options(test_mm_led_write PRIVATE -ffunction-sections -fdata-sections)
target_link_options(test_mm_led_write PRIVATE -Wl,--gc-sections)
target_link_libraries(test_mm_led_write PRIVATE pthread)
add_test(NAME mm_led_write COMMAND test_mm_led_write)

add_executable(test_wifi_prov test_wifi_prov.c ${CTL_DIR}/net/net_util.c
    ${CTL_DIR}/net/net_state.c
    ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c
    ${CTL_DIR}/../osif/linux/osif_timer_linux.c)
add_dependencies(test_wifi_prov gen_boardconf_nano3s)
target_include_directories(test_wifi_prov PRIVATE ${CTL_DIR}/net ${CTL_DIR}/ctrl
    ${CTL_DIR}/../osif ${GEN_DIR} ${CTL_DIR}/../hal/board)
target_include_directories(test_wifi_prov PRIVATE ${CTL_DIR}/../hal/platform/linux)
target_link_libraries(test_wifi_prov PRIVATE pthread)
add_test(NAME wifi_prov COMMAND test_wifi_prov)

add_executable(test_ble_wr test_ble_wr.c ${CTL_DIR}/net/ble_wr.c)
target_include_directories(test_ble_wr PRIVATE ${CTL_DIR}/net)
add_test(NAME ble_wr COMMAND test_ble_wr)

add_executable(test_net_util test_net_util.c ${CTL_DIR}/net/net_util.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c
    ${CTL_DIR}/../osif/linux/osif_timer_linux.c)
add_dependencies(test_net_util gen_boardconf_nano3s)
target_include_directories(test_net_util PRIVATE ${CTL_DIR}/net ${CTL_DIR}/ctrl
    ${CTL_DIR}/../osif ${GEN_DIR} ${CTL_DIR}/../hal/board)
target_link_libraries(test_net_util PRIVATE pthread)
add_test(NAME net_util COMMAND test_net_util)

add_executable(test_mm_children test_mm_children.c
    ${CTL_DIR}/../osif/linux/osif_timer_linux.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
add_dependencies(test_mm_children gen_boardconf_nano3s)
target_include_directories(test_mm_children PRIVATE
    ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
    ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif
    ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${GEN_DIR})
add_test(NAME mm_children COMMAND test_mm_children)

add_executable(test_tz test_tz.c
    ${COMMON_DIR}/ini/ini.c
    ${COMMON_DIR}/ini/ini_file.c
    ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
    ${CTL_DIR}/../miner_core/api/mc_api_bootby.c
    ${CTRL_CMDS_C} ${BOARDCONF_TABLES})
add_dependencies(test_tz gen_boardconf_nano3s)
target_include_directories(test_tz PRIVATE ${CTL_DIR}/../app ${COMMON_DIR}/ini
    ${CTL_DIR}/../osif ${CTL_DIR}/../miner_core/api ${CTL_DIR}/../common/proto
    ${CTL_DIR}/api
    ${GEN_DIR}
    ${CTL_DIR}/../hal/board)
target_compile_definitions(test_tz PRIVATE
    TZ_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/tz_fixture")
target_link_libraries(test_tz PRIVATE pthread)
add_test(NAME tz COMMAND test_tz)

add_executable(test_mm_cfgstore test_mm_cfgstore.c
    ${CTL_DIR}/../app/mm_cfgstore.c ${COMMON_DIR}/ini/ini_cache.c
    ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
    ${CTL_DIR}/../miner_core/api/mc_api_bootby.c)
target_include_directories(test_mm_cfgstore PRIVATE ${CTL_DIR}/../app ${COMMON_DIR}/ini
    ${CTL_DIR}/../miner_core/api)
target_link_libraries(test_mm_cfgstore PRIVATE pthread)
add_test(NAME mm_cfgstore COMMAND test_mm_cfgstore)

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_ctrl_${BOARD} test_mm_ctrl.c
        ${CTL_DIR}/../miner_core/api/mc_api_setter.c ${CTL_DIR}/../common/json/jread.c
        ${CTL_DIR}/../osif/linux/osif_log_linux.c
        ${BCT}
        ${CTRL_CMDS_C})
    add_dependencies(test_mm_ctrl_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_ctrl_${BOARD} PRIVATE ${CTL_DIR}/../app
        ${CTL_DIR}/../common/proto ${CTL_DIR}/../common/json ${CTL_DIR}/../miner_core/api
        ${CTL_DIR}/../miner_core ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../osif
        ${CTL_DIR}/cali
        ${CTL_DIR}/ctrl ${CTL_DIR}/../tests/fixtures ${BCDIR})
    add_test(NAME mm_ctrl_${BOARD} COMMAND test_mm_ctrl_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    foreach(TGT core_setters_${BOARD} core_setters_${BOARD}_noact)
        add_executable(test_${TGT} test_core_setters.c
            ${CTL_DIR}/../app/ctrl_args.c
            ${CTL_DIR}/../app/core_softonoff.c
            ${CTL_DIR}/../app/core_activate.c
            ${CTL_DIR}/../app/core_sigverify.c
            ${CTL_DIR}/../app/core_ident.c
            ${CTL_DIR}/cali/cali.c ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/util/cfg_u32.c
            ${CTL_DIR}/../common/proto/ctrl_cmds.c
            ${CTL_DIR}/../miner_core/api/mc_api_setter.c ${CTL_DIR}/../common/json/jread.c
            ${CTL_DIR}/../miner_core/api/mc_api_cmds.c
            ${CTL_DIR}/../miner_core/api/mc_api_cmds_json.c
            ${CTL_DIR}/../miner_core/api/mc_api_cmds_text.c
            ${CTL_DIR}/../miner_core/api/mc_api_estats.c
            ${CTL_DIR}/../miner_core/api/mc_api_lcd.c
            ${CTL_DIR}/../miner_core/api/mc_api_command.c
            ${CTL_DIR}/../miner_core/api/mc_api_identity.c
            ${CTL_DIR}/../hal/platform/linux/net_hwaddr.c
            ${CTL_DIR}/../miner_core/api/mc_api_qrlogin.c
            ${CTL_DIR}/../miner_core/api/mc_api_server.c
            ${CTL_DIR}/../miner_core/api/mc_api_time.c
            ${CTL_DIR}/api/webauth.c ${CTL_DIR}/../common/sha/sha256.c
            ${CTL_DIR}/../common/json/jwrite.c ${COMMON_DIR}/ini/ini_file.c
            ${BCT}
            ${CTRL_CMDS_C})
        add_dependencies(test_${TGT} gen_boardconf_${BOARD})
        target_include_directories(test_${TGT} PRIVATE ${CTL_DIR}/../app
            ${CTL_DIR}/../common/proto ${CTL_DIR}/../common/json ${CTL_DIR}/../osif
            ${CTL_DIR}/../miner_core/api ${CTL_DIR}/../miner_core
            ${CTL_DIR}/cali ${CTL_DIR}/ctrl ${COMMON_DIR}/ini
            ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board
            ${CTL_DIR}/../hal/platform/linux
            ${CTL_DIR}/api ${COMMON_DIR}/sha ${BCDIR})
        pic_pubkey_header(test_${TGT} ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem)
        target_compile_definitions(test_${TGT} PRIVATE
            CORE_SETTERS_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/${TGT}"
            PIC_SIGN_KEY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pem")
        target_link_libraries(test_${TGT} PRIVATE pthread OpenSSL::Crypto)
        add_test(NAME ${TGT} COMMAND test_${TGT})
    endforeach()
    target_include_directories(test_core_setters_${BOARD}_noact BEFORE PRIVATE
        ${CTL_DIR}/../tests/fixtures/cap_off)
    target_compile_definitions(test_core_setters_${BOARD}_noact PRIVATE TST_CAP_ACTIVATION_OFF)
endforeach()

add_executable(test_pool_setter test_pool_setter.c
    ${CTL_DIR}/../app/core_pool.c
    ${COMMON_DIR}/pool/pool_config.c
    ${COMMON_DIR}/pool/pool.c
    ${CTL_DIR}/../miner_core/api/mc_api_cmds.c
    ${CTL_DIR}/../miner_core/api/mc_api_cmds_json.c
    ${CTL_DIR}/../miner_core/api/mc_api_cmds_text.c
    ${CTL_DIR}/../miner_core/api/mc_api_estats.c
    ${CTL_DIR}/../miner_core/api/mc_api_lcd.c
    ${CTL_DIR}/../miner_core/api/mc_api_command.c
    ${CTL_DIR}/../miner_core/api/mc_api_identity.c
    ${CTL_DIR}/../hal/platform/linux/net_hwaddr.c
    ${CTL_DIR}/../miner_core/api/mc_api_qrlogin.c
    ${CTL_DIR}/../miner_core/api/mc_api_setter.c
    ${CTL_DIR}/../miner_core/api/mc_api_setpool.c
    ${CTL_DIR}/../miner_core/api/mc_api_time.c
    ${CTL_DIR}/api/webauth.c ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../common/json/jwrite.c ${CTL_DIR}/../common/json/jread.c
    ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c
    ${CTRL_CMDS_C} ${BOARDCONF_TABLES})
add_dependencies(test_pool_setter gen_boardconf_nano3s)
target_include_directories(test_pool_setter PRIVATE ${CTL_DIR}/../app
    ${CTL_DIR}/../common/proto ${CTL_DIR}/../common/json ${CTL_DIR}/../osif
    ${CTL_DIR}/../miner_core/api ${CTL_DIR}/../miner_core
    ${CTL_DIR}/../miner_core/stratum
    ${CTL_DIR}/api ${COMMON_DIR}/sha ${COMMON_DIR}/ini ${GEN_DIR}
    ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux)
target_link_libraries(test_pool_setter PRIVATE pthread)
add_test(NAME pool_setter COMMAND test_pool_setter)

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_fanrule_${BOARD} test_mm_fanrule.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c stub_wdt_dev.c
        ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c ${CTL_DIR}/net/net_state.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_fanrule_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_fanrule_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_link_libraries(test_mm_fanrule_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_fanrule_${BOARD} COMMAND test_mm_fanrule_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_wdt_${BOARD} test_mm_wdt.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_wdt_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_wdt_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_wdt_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_wdt_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_wdt_${BOARD} COMMAND test_mm_wdt_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_thermal_act_${BOARD} test_mm_thermal_act.c
        stub_mm_supervisor.c ${CTL_DIR}/ctrl/wdt_flags.c
        ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_thermal_act_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_thermal_act_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_thermal_act_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_thermal_act_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_thermal_act_${BOARD} COMMAND test_mm_thermal_act_${BOARD})
endforeach()


foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_rail_${BOARD} test_mm_rail.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c ${CTL_DIR}/net/net_state.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_rail_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_rail_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_rail_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_rail_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_rail_${BOARD} COMMAND test_mm_rail_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_logrot_${BOARD} test_mm_logrot.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c ${CTL_DIR}/net/net_state.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_logrot_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_logrot_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_logrot_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_logrot_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_logrot_${BOARD} COMMAND test_mm_logrot_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_periph_${BOARD} test_mm_periph.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c ${CTL_DIR}/net/net_state.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_periph_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_periph_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_periph_${BOARD} PRIVATE
        SUPPORT_LCD=1 TEST_RUNS_SUPERVISOR_LOOP=1)
    target_include_directories(test_mm_periph_${BOARD} PRIVATE ${CTL_DIR}/ui)
    target_link_libraries(test_mm_periph_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_periph_${BOARD} COMMAND test_mm_periph_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_net_${BOARD} test_mm_net.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c ${CTL_DIR}/net/net_state.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_net_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_net_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_net_${BOARD} PRIVATE TST_BOARD="${BOARD}")
    target_link_libraries(test_mm_net_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_net_${BOARD} COMMAND test_mm_net_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_api_act_${BOARD} test_mm_api_act.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_api_act_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_api_act_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_api_act_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_api_act_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_api_act_${BOARD} COMMAND test_mm_api_act_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_pool_gate_${BOARD} test_mm_pool_gate.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_pool_gate_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_pool_gate_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_pool_gate_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_pool_gate_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_pool_gate_${BOARD} COMMAND test_mm_pool_gate_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_respawn_${BOARD} test_mm_respawn.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_respawn_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_respawn_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_respawn_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_respawn_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_respawn_${BOARD} COMMAND test_mm_respawn_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_enum_arm_${BOARD} test_mm_enum_arm.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_enum_arm_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_enum_arm_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_enum_arm_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_enum_arm_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_enum_arm_${BOARD} COMMAND test_mm_enum_arm_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_fault_${BOARD} test_mm_fault.c stub_mm_supervisor.c
        ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
        ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
        ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
        ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
        ${CTL_DIR}/framework.c
        ${CTL_DIR}/../app/ctrl_args.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_fault_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_fault_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_mm_fault_${BOARD} PRIVATE
        TEST_RUNS_SUPERVISOR_LOOP=1)
    target_link_libraries(test_mm_fault_${BOARD} PRIVATE m pthread)
    add_test(NAME mm_fault_${BOARD} COMMAND test_mm_fault_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    foreach(SFX "" _onemode)
        add_executable(test_mm_route_${BOARD}${SFX} test_mm_route.c stub_mm_supervisor.c
            ${CTL_DIR}/ctrl/wdt_flags.c ${CTL_DIR}/../app/mm_wdt_sig.c
            ${CTL_DIR}/ctrl/fan_pid.c ${CTL_DIR}/ctrl/thermal.c
            ${CTL_DIR}/ctrl/softonoff.c ${CTL_DIR}/ctrl/enum_hold.c ${CTL_DIR}/ctrl/fan_health.c
            ${CTL_DIR}/ctrl/softonoff_sched.c ${CTL_DIR}/ctrl/net_select.c
            ${CTL_DIR}/net/net_state.c
            ${CTL_DIR}/framework.c
            ${CTL_DIR}/../app/ctrl_args.c
            ${BCT}
            ${CTRL_CMDS_C})
        add_dependencies(test_mm_route_${BOARD}${SFX} gen_boardconf_${BOARD})
        target_include_directories(test_mm_route_${BOARD}${SFX} PRIVATE
            ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
            ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
            ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api
            ${CTL_DIR}/../tests/fixtures ${BCDIR})
        target_compile_definitions(test_mm_route_${BOARD}${SFX} PRIVATE
            MM_ROUTE_TZ_DIR="${CMAKE_CURRENT_BINARY_DIR}/route_zoneinfo_${BOARD}${SFX}"
            MM_ROUTE_TZ_LINK="${CMAKE_CURRENT_BINARY_DIR}/route_localtime_${BOARD}${SFX}")
        target_link_libraries(test_mm_route_${BOARD}${SFX} PRIVATE m pthread)
        add_test(NAME mm_route_${BOARD}${SFX} COMMAND test_mm_route_${BOARD}${SFX})
    endforeach()
    target_include_directories(test_mm_route_${BOARD}_onemode BEFORE PRIVATE
        ${CTL_DIR}/../tests/fixtures/cap_off)
    target_compile_definitions(test_mm_route_${BOARD}_onemode PRIVATE TST_WORKMODE_SINGLE)
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_ctrl_plugin_${BOARD} test_ctrl_plugin.c
        ${CTL_DIR}/../app/ctrl_args.c ${CTL_DIR}/../tests/api_plugin_stub/ctrl_plugin_stub.c
        ${BCT} ${CTRL_CMDS_C})
    add_dependencies(test_ctrl_plugin_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_ctrl_plugin_${BOARD} PRIVATE
        ${CTL_DIR}/../tests/api_plugin_stub
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    target_compile_definitions(test_ctrl_plugin_${BOARD} PRIVATE
        CTRL_PLUGIN=1 "CTRL_PLUGIN_HEADER=\"ctrl_plugin_cmds.h\"")
    target_link_libraries(test_ctrl_plugin_${BOARD} PRIVATE pthread)
    add_test(NAME ctrl_plugin_${BOARD} COMMAND test_ctrl_plugin_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_system_${BOARD} test_mm_system.c
        ${CTL_DIR}/../miner_core/api/mc_api_bootby.c)
    add_dependencies(test_mm_system_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_system_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/api ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../miner_core/api ${BCDIR})
    add_test(NAME mm_system_${BOARD} COMMAND test_mm_system_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_telem_${BOARD} test_mm_telem.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_telem_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_telem_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${BCDIR})
    target_compile_options(test_mm_telem_${BOARD} PRIVATE
        -fsanitize=alignment -fno-sanitize-recover=alignment)
    target_link_options(test_mm_telem_${BOARD} PRIVATE -fsanitize=alignment)
    target_link_libraries(test_mm_telem_${BOARD} PRIVATE pthread)
    add_test(NAME mm_telem_${BOARD} COMMAND test_mm_telem_${BOARD})
endforeach()

foreach(BOARD ${MINER_TEST_BOARDS})
    miner_board_select(${BOARD} BCT BCDIR)
    add_executable(test_mm_vf_${BOARD} test_mm_vf.c
        ${CTL_DIR}/ctrl/pout_reg.c ${CTL_DIR}/ctrl/aging.c ${CTL_DIR}/ctrl/autotune.c
        ${CTL_DIR}/cali/cali.c ${CTL_DIR}/../common/util/cfg_u32.c
        ${CTL_DIR}/../common/ini/ini.c ${CTL_DIR}/../common/ini/ini_file.c
        ${CTL_DIR}/../common/proto/board_desc.c
        ${CTRL_CMDS_C} ${BCT})
    add_dependencies(test_mm_vf_${BOARD} gen_boardconf_${BOARD})
    target_include_directories(test_mm_vf_${BOARD} PRIVATE
        ${CTL_DIR}/../app ${CTL_DIR} ${CTL_DIR}/ctrl ${CTL_DIR}/cali
        ${CTL_DIR}/../hal ${CTL_DIR}/../hal ${CTL_DIR}/../hal/board ${CTL_DIR}/../hal/platform/linux
        ${CTL_DIR}/../osif ${CTL_DIR}/../common/proto ${CTL_DIR}/../common/ini ${BCDIR})
    target_link_libraries(test_mm_vf_${BOARD} PRIVATE pthread m)
    target_compile_definitions(test_mm_vf_${BOARD} PRIVATE
        TST_BIN_DIR="${CMAKE_CURRENT_BINARY_DIR}" TST_BOARD="${BOARD}")
    add_test(NAME mm_vf_${BOARD} COMMAND test_mm_vf_${BOARD})
endforeach()

add_executable(test_ota_state test_ota_state.c
    ${CTL_DIR}/ota/ota_state.c ${CTL_DIR}/ota/aup.c
    ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
target_include_directories(test_ota_state PRIVATE ${CTL_DIR}/ota
    ${CTL_DIR}/../common/sha)
add_test(NAME ota_state COMMAND test_ota_state)

add_executable(test_aup_verify test_aup_verify.c
    ${CTL_DIR}/ota/aup_verify.c
    ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
target_include_directories(test_aup_verify PRIVATE ${CTL_DIR}/ota
    ${CTL_DIR}/../common/sha)
target_compile_definitions(test_aup_verify PRIVATE
    AUP_VERIFY_SRC="${CTL_DIR}/ota/aup_verify.c")
target_link_libraries(test_aup_verify PRIVATE OpenSSL::Crypto)
add_test(NAME aup_verify COMMAND test_aup_verify)

add_executable(test_aup_verify_pem test_aup_verify_pem.c
    ${CTL_DIR}/ota/aup_verify.c
    ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
target_include_directories(test_aup_verify_pem PRIVATE ${CTL_DIR}/ota
    ${CTL_DIR}/../common/sha)
aup_pubkey_header(test_aup_verify_pem ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem)
target_compile_definitions(test_aup_verify_pem PRIVATE
    AUP_TEST_KEY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pem"
    AUP_DEV_KEY="${CTL_DIR}/../tools/aup-dev-signing-key.pem")
target_link_libraries(test_aup_verify_pem PRIVATE OpenSSL::Crypto)
add_test(NAME aup_verify_pem COMMAND test_aup_verify_pem)

set(AUP_PKG_DIR ${CMAKE_CURRENT_BINARY_DIR}/aup_pkg)
set(AUP_DEV_KEY ${CTL_DIR}/../tools/aup-dev-signing-key.pem)
file(REMOVE_RECURSE ${AUP_PKG_DIR})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CTL_DIR}/../tools/gen_aup.py ${CTL_DIR}/ota/aup_verify.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-package.json
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-package.bin
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem)
function(aup_test_package var name)
    file(MAKE_DIRECTORY ${AUP_PKG_DIR}/${name})
    execute_process(COMMAND python3 ${CTL_DIR}/../tools/gen_aup.py
        -f ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-package.json
        -i ${CMAKE_CURRENT_SOURCE_DIR}/fixtures -o ${AUP_PKG_DIR}/${name} ${ARGN}
        RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
    file(GLOB pkg ${AUP_PKG_DIR}/${name}/*.aup)
    list(LENGTH pkg n)
    if(NOT rc EQUAL 0 OR NOT n EQUAL 1)
        message(FATAL_ERROR "gen_aup.py failed for the ${name} test package: ${err}")
    endif()
    set(${var} ${pkg} PARENT_SCOPE)
endfunction()
aup_test_package(AUP_PKG_TEST test -k ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pem
    --verify-key ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem
    --firmware ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem)
set(AUP_PKG_DEV "")
if(EXISTS ${AUP_DEV_KEY})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${AUP_DEV_KEY})
    execute_process(COMMAND python3 -c "
import sys
from cryptography.hazmat.primitives import serialization as s
key = s.load_pem_private_key(open(sys.argv[1], 'rb').read(), None)
sys.stdout.buffer.write(key.public_key().public_bytes(
    s.Encoding.PEM, s.PublicFormat.SubjectPublicKeyInfo))" ${AUP_DEV_KEY}
        OUTPUT_FILE ${AUP_PKG_DIR}/dev-mm_miner
        RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "cannot derive the public key of ${AUP_DEV_KEY}: ${err}")
    endif()
    aup_test_package(AUP_PKG_DEV dev -k ${AUP_DEV_KEY}
        --firmware ${AUP_PKG_DIR}/dev-mm_miner)
endif()

add_executable(test_aup_package test_aup_package.c
    ${CTL_DIR}/ota/aup.c ${CTL_DIR}/ota/aup_verify.c
    ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
target_include_directories(test_aup_package PRIVATE ${CTL_DIR}/ota
    ${CTL_DIR}/../common/sha)
target_compile_definitions(test_aup_package PRIVATE
    AUP_PKG_GOOD="${AUP_PKG_DEV}" AUP_PKG_BAD="${AUP_PKG_TEST}")
target_link_libraries(test_aup_package PRIVATE OpenSSL::Crypto)
add_test(NAME aup_package COMMAND test_aup_package)

add_executable(test_aup_package_pem test_aup_package.c
    ${CTL_DIR}/ota/aup.c ${CTL_DIR}/ota/aup_verify.c
    ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
target_include_directories(test_aup_package_pem PRIVATE ${CTL_DIR}/ota
    ${CTL_DIR}/../common/sha)
aup_pubkey_header(test_aup_package_pem ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem)
target_compile_definitions(test_aup_package_pem PRIVATE
    AUP_PKG_GOOD="${AUP_PKG_TEST}" AUP_PKG_BAD="${AUP_PKG_DEV}")
target_link_libraries(test_aup_package_pem PRIVATE OpenSSL::Crypto)
add_test(NAME aup_package_pem COMMAND test_aup_package_pem)

set(CORE_SIGVERIFY_SRC test_core_sigverify.c
    ${CTL_DIR}/../app/core_activate.c ${CTL_DIR}/../app/core_sigverify.c
    ${CTL_DIR}/../common/sha/sha256.c
    ${CTL_DIR}/../osif/linux/osif_log_linux.c)
add_executable(test_core_sigverify ${CORE_SIGVERIFY_SRC})
target_include_directories(test_core_sigverify PRIVATE ${CTL_DIR}/../app
    ${CTL_DIR}/../common/sha)
target_compile_definitions(test_core_sigverify PRIVATE
    PIC_KEY_BAD="${AUP_DEV_KEY}")
target_link_libraries(test_core_sigverify PRIVATE OpenSSL::Crypto)
add_test(NAME core_sigverify COMMAND test_core_sigverify)

add_executable(test_core_sigverify_pem ${CORE_SIGVERIFY_SRC})
target_include_directories(test_core_sigverify_pem PRIVATE ${CTL_DIR}/../app
    ${CTL_DIR}/../common/sha)
pic_pubkey_header(test_core_sigverify_pem ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pub.pem)
target_compile_definitions(test_core_sigverify_pem PRIVATE
    PIC_KEY_GOOD="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aup-test-key.pem"
    PIC_KEY_BAD="${AUP_DEV_KEY}")
target_link_libraries(test_core_sigverify_pem PRIVATE OpenSSL::Crypto)
add_test(NAME core_sigverify_pem COMMAND test_core_sigverify_pem)

add_executable(test_mm_ota test_mm_ota.c)
target_include_directories(test_mm_ota PRIVATE ${CTL_DIR}/../app ${CTL_DIR}/ota
    ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif)
add_test(NAME mm_ota COMMAND test_mm_ota)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/wdtsig)
add_executable(test_mm_wdt_crash test_mm_wdt_crash.c
    ${CTL_DIR}/../app/mm_wdt_sig.c ${CTL_DIR}/ctrl/wdt_flags.c
    ${CTL_DIR}/../miner_core/api/mc_api_bootby.c
    ${COMMON_DIR}/ini/ini_cache.c
    ${COMMON_DIR}/ini/ini.c ${COMMON_DIR}/ini/ini_file.c)
add_dependencies(test_mm_wdt_crash gen_boardconf_nano3s)
target_include_directories(test_mm_wdt_crash PRIVATE ${CTL_DIR}/../app
    ${CTL_DIR}/ctrl ${CTL_DIR}/api ${CTL_DIR}/../osif ${CTL_DIR}/../hal/platform/linux
    ${CTL_DIR}/../miner_core/api ${COMMON_DIR}/ini ${GEN_DIR})
target_compile_definitions(test_mm_wdt_crash PRIVATE
    TST_BIN_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_mm_wdt_crash PRIVATE pthread)
add_test(NAME mm_wdt_crash COMMAND test_mm_wdt_crash)

# LVGL v9.2.2, the commit the lvgl recipe builds. FETCHCONTENT_SOURCE_DIR_LVGL
# names a local checkout of it instead of the download.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(lvgl
    URL https://github.com/lvgl/lvgl/archive/7f07a129e8d77f4984fff8e623fd5be18ff42e74.tar.gz
    URL_HASH SHA256=8a5e30223fd5b08cdeccb060f45b37a29868d6b1fdfc5bdbdf0dd89aaefb5a0e
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(lvgl)
# The UI includes "lvgl/lvgl.h".
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lvgl-include)
file(CREATE_LINK ${lvgl_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lvgl-include/lvgl SYMBOLIC)
set(UI_LVGL_FILES
    ${CTL_DIR}/../../../product/k230/miner/avalon-home/nano3s/recipes-graphics/lvgl/files)
file(GLOB_RECURSE UI_LVGL_SOURCES ${lvgl_SOURCE_DIR}/src/*.c)
add_library(test_ui_lvgl STATIC ${UI_LVGL_SOURCES})
target_include_directories(test_ui_lvgl PUBLIC ${UI_LVGL_FILES}
    ${CMAKE_CURRENT_BINARY_DIR}/lvgl-include ${lvgl_SOURCE_DIR})
target_compile_definitions(test_ui_lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE=1)
add_executable(test_ui_wallpaper test_ui_wallpaper.c ${CTL_DIR}/ui/clock_font_40.c
    ${CTL_DIR}/ui/ui_dispatch.c)
target_include_directories(test_ui_wallpaper PRIVATE ${CTL_DIR}/ui)
target_compile_definitions(test_ui_wallpaper PRIVATE SUPPORT_LCD=1)
target_link_libraries(test_ui_wallpaper PRIVATE test_ui_lvgl m)
add_test(NAME ui_wallpaper COMMAND test_ui_wallpaper)

add_executable(test_ui_fault_screen test_ui_fault_screen.c ${CTL_DIR}/ui/clock_font_40.c
    ${CTL_DIR}/ui/ui_dispatch.c)
target_include_directories(test_ui_fault_screen PRIVATE ${CTL_DIR}/ui)
target_compile_definitions(test_ui_fault_screen PRIVATE SUPPORT_LCD=1)
target_link_libraries(test_ui_fault_screen PRIVATE test_ui_lvgl m)
add_test(NAME ui_fault_screen COMMAND test_ui_fault_screen)

add_executable(test_ui_backlight test_ui_backlight.c)
add_dependencies(test_ui_backlight gen_boardconf_nano3s)
target_include_directories(test_ui_backlight PRIVATE ${GEN_DIR}
    ${CTL_DIR}/../hal/ui ${CTL_DIR}/../hal/platform/linux ${CTL_DIR}/../osif)
target_link_libraries(test_ui_backlight PRIVATE pthread)
add_test(NAME ui_backlight COMMAND test_ui_backlight)

add_executable(test_mm_efuse test_mm_efuse.c ${CTL_DIR}/../app/mm_efuse.c)
add_dependencies(test_mm_efuse gen_boardconf_nano3s)
target_include_directories(test_mm_efuse PRIVATE ${GEN_DIR}
    ${CTL_DIR}/../app ${CTL_DIR}/../osif ${CTL_DIR}/ctrl)
target_link_libraries(test_mm_efuse PRIVATE pthread)
add_test(NAME mm_efuse COMMAND test_mm_efuse)

get_property(ALL_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${ALL_TESTS} PROPERTIES SKIP_RETURN_CODE ${TST_SKIP})
