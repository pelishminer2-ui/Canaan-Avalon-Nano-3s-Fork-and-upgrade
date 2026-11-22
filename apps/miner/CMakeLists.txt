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
project(canaan_miner C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror=format -Wno-error=format-truncation -O2)

set(M ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenSSL REQUIRED)

include(${M}/cmake/MinerVersion.cmake)
include(${M}/cmake/MinerProtoVer.cmake)
set(FW_TYPE "Debug" CACHE STRING "firmware type")
set(BOARD_DESC "" CACHE FILEPATH "lay1 board descriptor (boardconf.ini)")
add_compile_definitions(FW_TYPE="${FW_TYPE}" MINER_PROTO_VER="${MINER_PROTO_VER}")
add_compile_definitions(CORE_MINER_VERSION="${MINER_BUILD_VERSION}"
    CORE_MINER_SEMVER="${FW_VERSION}"
    MM_MINER_VERSION="${MINER_BUILD_VERSION}"
    ASIC_MINER_VERSION="${MINER_BUILD_VERSION}")

if(NOT BOARD_DESC)
    set(BOARD_DESC ${M}/../../product/k230/miner/avalon-home/nano3s/conf/product/boardconf.ini)
endif()

set(BOARDCONF_H ${CMAKE_CURRENT_BINARY_DIR}/gen/boardconf.h)
set(BOARDCONF_TABLES ${M}/hal/board/board_tables.c)
add_custom_command(OUTPUT ${BOARDCONF_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gen
    COMMAND python3 ${M}/tools/gen_boardconf.py ${BOARD_DESC} ${BOARDCONF_H}
    DEPENDS ${BOARD_DESC} ${M}/tools/gen_boardconf.py
    COMMENT "gen boardconf from ${BOARD_DESC}")
include_directories(${M}/hal/board)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BOARD_DESC})
foreach(feature SUPPORT_LCD SUPPORT_BLE)
    execute_process(
        COMMAND python3 ${M}/tools/gen_boardconf.py --get ${feature} ${BOARD_DESC}
        OUTPUT_VARIABLE value
        ERROR_VARIABLE err
        RESULT_VARIABLE rc
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${feature}: ${err}")
    endif()
    if(DEFINED CACHE{${feature}})
        if((${feature} AND NOT value) OR (value AND NOT ${feature}))
            message(FATAL_ERROR "-D${feature}=${${feature}} disagrees with "
                "${feature} = ${value} in ${BOARD_DESC}")
        endif()
    endif()
    set(${feature} ${value})
endforeach()

option(API_PLUGIN "add the ascset settings and control ops of API_PLUGIN_DIR to the miner" OFF)
set(API_PLUGIN_DIR "" CACHE PATH
    "directory holding api_plugin.cmake, api_plugin_cmds.h and ctrl_plugin_cmds.h")
if(API_PLUGIN)
    foreach(f api_plugin.cmake api_plugin_cmds.h ctrl_plugin_cmds.h)
        if(NOT EXISTS "${API_PLUGIN_DIR}/${f}")
            message(FATAL_ERROR "API_PLUGIN: ${API_PLUGIN_DIR}/${f} not found")
        endif()
    endforeach()
    include_directories(${API_PLUGIN_DIR})
    add_compile_definitions(CTRL_PLUGIN=1 "CTRL_PLUGIN_HEADER=\"ctrl_plugin_cmds.h\"")
endif()

add_library(mc_common STATIC
    common/sha/sha256.c common/ini/ini.c common/ini/ini_file.c common/ini/ini_cache.c
    common/json/jread.c common/json/jwrite.c
    common/proto/board_desc.c common/proto/ctrl_cmds.c common/util/cfg_u32.c
    ${BOARDCONF_H} ${BOARDCONF_TABLES})
target_include_directories(mc_common PUBLIC
    ${M}/common/sha ${M}/common/ini ${M}/common/log
    ${M}/common/json ${M}/common/proto ${M}/common/util
    ${M}/osif
    ${CMAKE_CURRENT_BINARY_DIR}/gen)

add_subdirectory(osif)

add_library(mc_cfg STATIC
    common/pool/pool.c common/pool/pool_config.c
    common/cfgwr/cfgwr_client.c common/cfgwr/cfgwr_io.c)
target_include_directories(mc_cfg PUBLIC ${M}/common/pool ${M}/common/cfgwr)
target_link_libraries(mc_cfg PUBLIC mc_common)

add_library(mc_mgmt_shared STATIC
    miner_mgmt/cali/cali.c miner_mgmt/api/webauth.c
    ${BOARDCONF_H})
target_include_directories(mc_mgmt_shared PRIVATE
    ${M}/miner_mgmt/cali ${M}/miner_mgmt/api)
target_link_libraries(mc_mgmt_shared PRIVATE mc_common pthread)

add_library(mc_ctrl_args STATIC app/ctrl_args.c ${BOARDCONF_H})
target_include_directories(mc_ctrl_args PRIVATE
    ${M}/app ${M}/miner_mgmt/cali ${M}/miner_mgmt/ctrl)
target_link_libraries(mc_ctrl_args PRIVATE mc_common)

add_library(mc_hal STATIC
    hal/asic/a3197s/a3197s_pvt.c hal/asic/a3197s/a3197s_pll.c hal/asic/a3197s/a3197s_cpm_table.c
    hal/asic/a3197s/uart_pro.c hal/asic/a3197s/a3197s_ops.c
    hal/asic/a3197s/a3197s_config.c hal/asic/a3197s/a3197s_pll_seq.c
    hal/platform/linux/adc_iio.c hal/platform/linux/gpio_dev.c
    hal/platform/linux/i2c_dev.c hal/platform/linux/pwm_sys.c
    hal/platform/linux/board_sensors.c
    hal/platform/linux/psu_dcdc_pd.c
    hal/platform/linux/temp_adc_ntc.c
    hal/platform/linux/net_linux_common.c
    hal/platform/linux/net_wired.c
    hal/platform/linux/net_wifi.c
    hal/platform/linux/net_wired_alt.c
    hal/platform/linux/net_ident.c
    hal/platform/linux/net_hwaddr.c
    hal/platform/linux/otp_dev.c
    hal/platform/linux/hashmcu.c
    hal/platform/linux/eeprom_dev.c
    hal/platform/linux/fan_dev.c
    hal/platform/linux/timer.c
    hal/platform/linux/btn_input.c
    hal/platform/linux/beep_dev.c
    hal/platform/linux/ws2812.c
    hal/ui/ui_backlight.c
    ${BOARDCONF_H})
target_include_directories(mc_hal PUBLIC
    ${M}/hal ${M}/hal/asic/a3197s ${M}/hal/platform/linux ${M}/hal/ui
    ${M}/hal/board
    ${M}/osif ${M}/common/proto
    ${CMAKE_CURRENT_BINARY_DIR}/gen)
target_link_libraries(mc_hal PUBLIC mc_common osif pthread m)

option(DEBUG_UART "hex-dump a3197s UART frames to the log" OFF)
if(DEBUG_UART)
    target_compile_definitions(mc_hal PRIVATE DEBUG_UART=1)
endif()

add_library(mc_core STATIC
    miner_core/work.c miner_core/share.c miner_core/pipeline.c
    miner_core/driver_avalon.c miner_core/avalon_gen.c miner_core/nonce_ring.c
    miner_core/stratum/sv1.c
    miner_core/stratum/stratum_net.c)
target_include_directories(mc_core PUBLIC
    ${M}/miner_core ${M}/miner_core/stratum ${M}/common/json ${M}/common/sha
    ${M}/common/proto ${M}/hal)
target_link_libraries(mc_core PUBLIC mc_cfg mc_common mc_hal pthread)

add_library(mc_mgmt STATIC
    miner_mgmt/framework.c miner_mgmt/ctrl/thermal.c
    miner_mgmt/ctrl/fan_pid.c miner_mgmt/ctrl/fan_health.c
    miner_mgmt/ctrl/pout_reg.c
    miner_mgmt/ctrl/aging.c
    miner_mgmt/ctrl/autotune.c
    miner_mgmt/ctrl/led_fx.c
    miner_mgmt/ctrl/softonoff_sched.c
    miner_mgmt/ctrl/net_select.c
    miner_mgmt/ctrl/softonoff.c
    miner_mgmt/ctrl/enum_hold.c
    miner_mgmt/ota/aup.c miner_mgmt/ota/aup_verify.c miner_mgmt/ota/ota_state.c
    miner_mgmt/net/net_discover.c miner_mgmt/net/net_state.c)
target_include_directories(mc_mgmt PUBLIC
    ${M}/miner_mgmt ${M}/miner_mgmt/ctrl ${M}/miner_mgmt/api
    ${M}/miner_mgmt/cali ${M}/miner_mgmt/ota ${M}/common/ini ${M}/common/json
    ${M}/hal/board ${M}/hal/platform/linux ${M}/miner_mgmt/net)
target_link_libraries(mc_mgmt PUBLIC mc_mgmt_shared mc_common m OpenSSL::Crypto osif pthread)
set(AUP_VERIFY_PUBKEY_PEM "" CACHE FILEPATH
    "PEM public key mm_miner verifies .aup signatures with; empty keeps the key in miner_mgmt/ota/aup_verify.c")
if(AUP_VERIFY_PUBKEY_PEM)
    include(${M}/cmake/AupPubkey.cmake)
    aup_pubkey_header(mc_mgmt "${AUP_VERIFY_PUBKEY_PEM}")
endif()

target_sources(mc_core PRIVATE
    miner_core/api/mc_api_server.c miner_core/api/mc_api_cmds.c miner_core/api/mc_api_estats.c
    miner_core/api/mc_api_lcd.c miner_core/api/mc_api_cmds_json.c miner_core/api/mc_api_cmds_text.c
    miner_core/api/mc_api_identity.c miner_core/api/mc_api_setter.c
    miner_core/api/mc_api_qrlogin.c miner_core/api/mc_api_time.c
    miner_core/api/mc_api_command.c
    miner_core/api/mc_api_setpool.c
    miner_core/api/mc_api_poolid.c
    miner_core/api/mc_api_addpool.c
    miner_core/api/mc_api_poollist.c)
target_include_directories(mc_core PUBLIC ${M}/miner_core/api ${M}/osif)
target_link_libraries(mc_core PUBLIC pthread osif)

add_executable(core_miner app/core_miner_main.c
    app/core_stats.c app/core_nonce.c app/core_stratum.c app/core_pool_cmds.c
    app/core_ascset.c
    app/core_activate.c
    app/core_sigverify.c
    app/core_wallpaper.c
    app/core_pool.c
    app/core_softonoff.c
    app/core_ctrl.c
    app/core_ident.c)
target_include_directories(core_miner PRIVATE ${M}/app ${M}/common/proto
    miner_mgmt/cali common/ini
    miner_mgmt/api common/sha
    miner_mgmt/ctrl)
target_link_libraries(core_miner PRIVATE mc_core mc_cfg mc_mgmt_shared mc_ctrl_args osif mc_common
    mc_hal OpenSSL::Crypto)
set(PIC_VERIFY_PUBKEY_PEM "" CACHE FILEPATH
    "API verify key: PEM public key core_miner verifies activation and wallpaper signatures with; empty keeps the key in app/core_sigverify.c")
if(PIC_VERIFY_PUBKEY_PEM)
    include(${M}/cmake/AupPubkey.cmake)
    pic_pubkey_header(core_miner "${PIC_VERIFY_PUBKEY_PEM}")
endif()

if(API_PLUGIN)
    include("${API_PLUGIN_DIR}/api_plugin.cmake")
    target_compile_definitions(core_miner PRIVATE
        API_PLUGIN=1 "API_PLUGIN_HEADER=\"api_plugin_cmds.h\"")
endif()

option(MINER_CORE_ONLY "configure core_miner only; mm_miner and asic_miner are not defined" OFF)
if(MINER_CORE_ONLY)
    return()
endif()

add_executable(mm_miner app/mm_miner_main.c app/mm_telem.c
    app/mm_ctrl.c
    app/mm_periph.c
    app/mm_softoff.c
    app/mm_power.c
    app/mm_spawn.c
    app/mm_children.c
    app/mm_wdt.c
    app/mm_system.c
    app/mm_vf.c
    app/mm_fan.c
    app/mm_tune.c
    app/mm_net.c
    app/mm_tz.c
    app/mm_logrot.c
    app/mm_model.c
    app/mm_ota.c
    app/mm_cfgwr.c
    app/mm_factory.c
    app/mm_cfgstore.c
    app/mm_efuse.c
    app/mm_led.c
    app/mm_wdt_sig.c
    miner_core/api/mc_api_bootby.c
    miner_mgmt/ctrl/wdt_flags.c
    hal/platform/linux/wdt_dev.c
    hal/platform/linux/mtd_dev.c
    hal/platform/linux/ubootenv.c
    ${API_PLUGIN_MM_SOURCES})
target_include_directories(mm_miner PRIVATE ${M}/app ${M}/common/proto
    ${M}/miner_core/api
    ${M}/common/json)
target_link_libraries(mm_miner PRIVATE mc_mgmt mc_cfg mc_mgmt_shared mc_ctrl_args mc_hal osif
    ubootenv)
target_link_options(mm_miner PRIVATE -rdynamic)

if(SUPPORT_BLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_sources(mc_mgmt PRIVATE
        miner_mgmt/net/ble_prov_dbus.c miner_mgmt/net/ble_wr.c
        miner_mgmt/net/wifi_mgr.c miner_mgmt/net/net_util.c)
    target_link_libraries(mc_mgmt PUBLIC PkgConfig::GIO)
    target_compile_definitions(mc_mgmt PUBLIC SUPPORT_BLE=1)
    target_sources(mm_miner PRIVATE app/mm_ble.c)
endif()

if(SUPPORT_LCD)
    target_sources(mm_miner PRIVATE
        app/mm_ui_glue.c app/mm_ui_cfg.c app/mm_display.c
        miner_mgmt/ui/ui.c miner_mgmt/ui/ui_screen.c
        miner_mgmt/ui/ui_dispatch.c miner_mgmt/ui/clock_font_40.c
        miner_mgmt/input/btn_decode.c)
    target_include_directories(mm_miner PRIVATE
        ${M}/app ${M}/miner_mgmt/ui ${M}/miner_mgmt/input
        ${M}/hal ${M}/hal/ui)
    target_compile_definitions(mm_miner PRIVATE SUPPORT_LCD=1 LV_CONF_INCLUDE_SIMPLE)
    target_link_libraries(mm_miner PRIVATE mc_hal lvgl)
endif()

add_executable(asic_miner app/asic_miner_main.c app/asic_telem.c app/asic_efuse.c)
target_compile_definitions(mm_miner PRIVATE MINER_PRODUCT_BUILD=1)
target_link_libraries(asic_miner PRIVATE mc_hal mc_core osif mc_common)
