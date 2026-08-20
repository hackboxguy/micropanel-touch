cmake_minimum_required(VERSION 3.21)

project(micropanel-touch VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

option(INSTALL_SYSTEMD_SERVICE "Install the micropanel-touch systemd units" OFF)
set(MICROPANEL_TOUCH_SERVICE_USER "micropanel-touch" CACHE STRING
    "Appliance account used by the installed systemd services")
set(MICROPANEL_TOUCH_BROKER_SOCKET "/run/micropanel-touch/broker.sock" CACHE FILEPATH
    "Absolute AF_UNIX socket path owned by the privileged broker")
set(MICROPANEL_TOUCH_DATA_DIR "/data/micropanel-touch" CACHE PATH
    "Persistent data directory supplied by the image data partition")
set(MICROPANEL_TOUCH_FALLBACK_DATA_DIR "/run/micropanel-touch-ui/data" CACHE PATH
    "Volatile fallback data directory when the persistent data partition is unavailable")
set(MICROPANEL_TOUCH_RUNTIME_DIR "/run/micropanel-touch-ui" CACHE PATH
    "Volatile runtime directory owned by the UI service")
set(MICROPANEL_TOUCH_STATIC_IP_INTERFACE "eth0" CACHE STRING
    "Default interface used by the installed static-IP UI")
set(SYSTEMD_UNITFILE_ARGS "" CACHE STRING
    "Extra arguments passed to micropanel-touch by the installed systemd service")

foreach(MICROPANEL_TOUCH_REQUIRED_ABSOLUTE_PATH
        MICROPANEL_TOUCH_BROKER_SOCKET
        MICROPANEL_TOUCH_DATA_DIR
        MICROPANEL_TOUCH_FALLBACK_DATA_DIR
        MICROPANEL_TOUCH_RUNTIME_DIR)
    if(NOT IS_ABSOLUTE "${${MICROPANEL_TOUCH_REQUIRED_ABSOLUTE_PATH}}")
        message(FATAL_ERROR "${MICROPANEL_TOUCH_REQUIRED_ABSOLUTE_PATH} must be an absolute path")
    endif()
endforeach()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiod)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)

# LVGL is the sole vendored dependency.  It is a pinned git submodule and must
# already be present: image builds never fetch dependencies at configure time.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/lvgl/CMakeLists.txt")
    message(FATAL_ERROR
        "LVGL submodule is missing. Run: git submodule update --init --recursive")
endif()

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config/lv_conf.h" CACHE PATH
    "micropanel-touch LVGL configuration" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/lvgl EXCLUDE_FROM_ALL)

add_library(micropanel-touch-core
    src/core/ActionCompiler.cpp
    src/core/ActionRunner.cpp
    src/core/LegacyConfig.cpp
    src/core/NavigationHistory.cpp
    src/core/PrivilegedOperations.cpp
    src/core/StaticIpSettings.cpp
    src/core/UiEventQueue.cpp
)
target_include_directories(micropanel-touch-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${NLOHMANN_JSON_INCLUDE_DIR}"
)
target_compile_features(micropanel-touch-core PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(micropanel-touch-core PUBLIC Threads::Threads)

add_library(micropanel-touch-platform
    src/platform/AboutInfo.cpp
    src/platform/ActionService.cpp
    src/platform/CommandService.cpp
    src/platform/CommandRunner.cpp
    src/platform/ControlServer.cpp
    src/platform/DisplayBackend.cpp
    src/platform/DisplayBrightnessSettings.cpp
    src/platform/DisplaySleep.cpp
    src/platform/DisplayStandbySettings.cpp
    src/platform/FrameCapture.cpp
    src/platform/HeadlessDisplay.cpp
    src/platform/NetworkInfo.cpp
    src/platform/PrivilegedBroker.cpp
    src/platform/NetworkApplyService.cpp
    src/platform/SystemStats.cpp
    src/platform/SystemUpdateService.cpp
    src/platform/PanelProfile.cpp
    src/platform/ScreenLockSettings.cpp
    src/platform/SettingsFile.cpp
    src/platform/StorageHealth.cpp
    src/platform/TouchCalibration.cpp
    src/platform/SyntheticKeypadInput.cpp
    src/platform/SyntheticTouchInput.cpp
    src/platform/TouchInput.cpp
    src/platform/WifiScan.cpp
)
target_include_directories(micropanel-touch-platform
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${NLOHMANN_JSON_INCLUDE_DIR}"
)
target_compile_features(micropanel-touch-platform PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-platform PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(micropanel-touch-platform
    PUBLIC
        micropanel-touch-core
        Threads::Threads
        PkgConfig::DRM
        PkgConfig::GPIOD
        OpenSSL::Crypto
        lvgl::lvgl
)

add_library(micropanel-touch-starter-config src/ui/StarterConfig.cpp)
target_include_directories(micropanel-touch-starter-config
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${NLOHMANN_JSON_INCLUDE_DIR}"
)
target_compile_features(micropanel-touch-starter-config PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-starter-config PRIVATE -Wall -Wextra -Wpedantic)

add_library(micropanel-touch-theme src/ui/UiTheme.cpp)
target_include_directories(micropanel-touch-theme
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${NLOHMANN_JSON_INCLUDE_DIR}"
)
target_compile_features(micropanel-touch-theme PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-theme PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(micropanel-touch-theme PUBLIC lvgl::lvgl)

add_library(micropanel-touch-icons src/ui/BuiltinIcon.cpp)
target_include_directories(micropanel-touch-icons PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(micropanel-touch-icons PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-icons PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(micropanel-touch-icons PUBLIC lvgl::lvgl)

add_library(micropanel-touch-ui
    src/ui/LegacyUi.cpp
    src/ui/StarterUi.cpp
)
target_link_libraries(micropanel-touch-ui PUBLIC micropanel-touch-platform micropanel-touch-starter-config
    micropanel-touch-theme micropanel-touch-icons)
target_compile_features(micropanel-touch-ui PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-ui PRIVATE -Wall -Wextra -Wpedantic)

add_executable(micropanel-touch
    src/main.cpp
    src/ui/HelloScreen.cpp
)
target_link_libraries(micropanel-touch PRIVATE micropanel-touch-ui)
target_compile_options(micropanel-touch PRIVATE -Wall -Wextra -Wpedantic)

add_executable(micropanel-touch-privileged src/privileged_main.cpp)
target_link_libraries(micropanel-touch-privileged PRIVATE micropanel-touch-platform)
target_compile_options(micropanel-touch-privileged PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS micropanel-touch RUNTIME DESTINATION usr/bin)
install(TARGETS micropanel-touch-privileged RUNTIME DESTINATION usr/sbin)
install(DIRECTORY screens/ DESTINATION share/micropanel-touch/screens)
install(PROGRAMS handlers/micropanel-touch-simulated-flash DESTINATION usr/bin)
# The A/B update handler now lives in the board-agnostic pi-ab-update engine
# (misc-tools/packages/pi-ab-update) and is installed by the image build, not
# from here. This application keeps only its trigger surface and its health
# hook.
install(PROGRAMS handlers/micropanel-touch-network-static-ip
                 handlers/micropanel-touch-network-dhcp
                 handlers/micropanel-touch-network-dhcp-server
                 handlers/micropanel-touch-power
                 handlers/micropanel-touch-wifi-join
                 handlers/micropanel-touch-wifi-forget DESTINATION usr/bin)
install(FILES LICENSE README.md DESTINATION share/doc/micropanel-touch)
install(FILES packaging/sysusers.d/micropanel-touch.conf DESTINATION usr/lib/sysusers.d)
install(FILES packaging/sysctl.d/micropanel-touch-console.conf DESTINATION usr/lib/sysctl.d)
install(FILES packaging/polkit-1/rules.d/49-micropanel-touch-network-manager.rules
    DESTINATION share/micropanel-touch/polkit)
install(FILES assets/panels/luckfox-ctp/st7796s.bin
    DESTINATION share/micropanel-touch/panels/luckfox-ctp)
install(PROGRAMS tools/deploy.sh tools/bootstrap-pi.sh tools/enable-piscreen.sh
    tools/enable-luckfox-ctp.sh
    tools/smoke-systemd-service.sh
    packaging/micropanel-touch-restore-machine-id
    packaging/micropanel-touch-update-health
    DESTINATION usr/share/micropanel-touch/tools)

if(INSTALL_SYSTEMD_SERVICE)
    configure_file(micropanel-touch.service.in micropanel-touch.service @ONLY)
    configure_file(micropanel-touch-privileged.service.in
        micropanel-touch-privileged.service @ONLY)
    configure_file(micropanel-touch-dhcp-server.service.in
        micropanel-touch-dhcp-server.service @ONLY)
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/micropanel-touch.service"
        "${CMAKE_CURRENT_BINARY_DIR}/micropanel-touch-privileged.service"
        "${CMAKE_CURRENT_BINARY_DIR}/micropanel-touch-dhcp-server.service"
        DESTINATION lib/systemd/system)
endif()

if(BUILD_TESTING)
    file(GLOB LEGACY_CONFIG_FIXTURES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy/*.json")
    list(SORT LEGACY_CONFIG_FIXTURES)
    list(LENGTH LEGACY_CONFIG_FIXTURES LEGACY_CONFIG_FIXTURE_COUNT)
    if(NOT LEGACY_CONFIG_FIXTURE_COUNT EQUAL 14)
        message(FATAL_ERROR "Expected 14 pinned legacy config fixtures")
    endif()

    add_executable(test-touch-mapper tests/test_touch_mapper.cpp)
    target_link_libraries(test-touch-mapper PRIVATE micropanel-touch-platform)
    target_compile_options(test-touch-mapper PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME touch-mapper COMMAND test-touch-mapper)

    add_executable(test-touch-calibration tests/test_touch_calibration.cpp)
    target_link_libraries(test-touch-calibration PRIVATE micropanel-touch-platform)
    target_compile_options(test-touch-calibration PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME touch-calibration COMMAND test-touch-calibration)

    add_executable(test-panel-profile tests/test_panel_profile.cpp)
    target_link_libraries(test-panel-profile PRIVATE micropanel-touch-platform)
    target_compile_options(test-panel-profile PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME panel-profile COMMAND test-panel-profile)

    add_executable(test-display-backend tests/test_display_backend.cpp)
    target_link_libraries(test-display-backend PRIVATE micropanel-touch-platform)
    target_compile_options(test-display-backend PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME display-backend COMMAND test-display-backend)

    add_executable(test-display-sleep tests/test_display_sleep.cpp)
    target_link_libraries(test-display-sleep PRIVATE micropanel-touch-platform)
    target_compile_options(test-display-sleep PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME display-sleep COMMAND test-display-sleep)

    add_executable(test-display-standby-settings tests/test_display_standby_settings.cpp)
    target_link_libraries(test-display-standby-settings PRIVATE micropanel-touch-platform)
    target_compile_options(test-display-standby-settings PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME display-standby-settings COMMAND test-display-standby-settings)

    add_executable(test-display-brightness-settings tests/test_display_brightness_settings.cpp)
    target_link_libraries(test-display-brightness-settings PRIVATE micropanel-touch-platform)
    target_compile_options(test-display-brightness-settings PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME display-brightness-settings COMMAND test-display-brightness-settings)

    add_executable(test-screen-lock-settings tests/test_screen_lock_settings.cpp)
    target_link_libraries(test-screen-lock-settings PRIVATE micropanel-touch-platform)
    target_compile_options(test-screen-lock-settings PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME screen-lock-settings COMMAND test-screen-lock-settings)

    add_executable(test-ui-event-queue tests/test_ui_event_queue.cpp)
    target_link_libraries(test-ui-event-queue PRIVATE micropanel-touch-core)
    target_compile_options(test-ui-event-queue PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ui-event-queue COMMAND test-ui-event-queue)

    add_executable(test-action-runner tests/test_action_runner.cpp)
    target_link_libraries(test-action-runner PRIVATE micropanel-touch-core)
    target_compile_options(test-action-runner PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME action-runner COMMAND test-action-runner
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/action-runner")

    add_executable(test-action-compiler tests/test_action_compiler.cpp)
    target_link_libraries(test-action-compiler PRIVATE micropanel-touch-core)
    target_compile_options(test-action-compiler PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME action-compiler COMMAND test-action-compiler)

    add_executable(test-static-ip-settings tests/test_static_ip_settings.cpp)
    target_link_libraries(test-static-ip-settings PRIVATE micropanel-touch-core)
    target_compile_options(test-static-ip-settings PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME static-ip-settings COMMAND test-static-ip-settings)

    add_executable(test-privileged-operations tests/test_privileged_operations.cpp)
    target_link_libraries(test-privileged-operations PRIVATE micropanel-touch-core)
    target_compile_options(test-privileged-operations PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME privileged-operations COMMAND test-privileged-operations)

    add_executable(test-navigation-history tests/test_navigation_history.cpp)
    target_link_libraries(test-navigation-history PRIVATE micropanel-touch-core)
    target_compile_options(test-navigation-history PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME navigation-history COMMAND test-navigation-history)

    add_executable(test-legacy-config tests/test_legacy_config.cpp)
    target_link_libraries(test-legacy-config PRIVATE micropanel-touch-core)
    target_compile_options(test-legacy-config PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME legacy-config COMMAND test-legacy-config
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy-config-basic.json")

    add_executable(test-legacy-config-corpus tests/test_legacy_config_corpus.cpp)
    target_link_libraries(test-legacy-config-corpus PRIVATE micropanel-touch-core)
    target_compile_options(test-legacy-config-corpus PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME legacy-config-corpus COMMAND test-legacy-config-corpus ${LEGACY_CONFIG_FIXTURES})
    add_test(NAME legacy-config-fixture-drift
        COMMAND "${CMAKE_COMMAND}"
            "-DLEGACY_CONFIG_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_legacy_config_manifest.cmake")
    add_test(NAME validate-config-cli
        COMMAND "${CMAKE_COMMAND}"
            "-DMICROPANEL_TOUCH_BINARY=$<TARGET_FILE:micropanel-touch>"
            "-DLEGACY_CONFIG_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_validate_config_cli.cmake")

    add_executable(test-starter-config tests/test_starter_config.cpp)
    target_link_libraries(test-starter-config PRIVATE micropanel-touch-starter-config)
    target_compile_options(test-starter-config PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME starter-config COMMAND test-starter-config
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/config-basic.json")

    add_executable(test-ui-theme tests/test_ui_theme.cpp)
    target_link_libraries(test-ui-theme PRIVATE micropanel-touch-theme)
    target_compile_options(test-ui-theme PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ui-theme COMMAND test-ui-theme
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/themes/dark.json")

    add_executable(test-builtin-icon tests/test_builtin_icon.cpp)
    target_link_libraries(test-builtin-icon PRIVATE micropanel-touch-icons)
    target_compile_options(test-builtin-icon PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME builtin-icon COMMAND test-builtin-icon)

    add_executable(test-wifi-scan tests/test_wifi_scan.cpp)
    target_link_libraries(test-wifi-scan PRIVATE micropanel-touch-platform)
    target_compile_options(test-wifi-scan PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME wifi-scan COMMAND test-wifi-scan)

    add_executable(test-network-info tests/test_network_info.cpp)
    target_link_libraries(test-network-info PRIVATE micropanel-touch-platform)
    target_compile_options(test-network-info PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME network-info COMMAND test-network-info)

    add_executable(test-command-runner tests/test_command_runner.cpp)
    target_link_libraries(test-command-runner PRIVATE micropanel-touch-platform)
    target_compile_options(test-command-runner PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME command-runner COMMAND test-command-runner)

    add_executable(test-command-service tests/test_command_service.cpp)
    target_link_libraries(test-command-service PRIVATE micropanel-touch-platform)
    target_compile_options(test-command-service PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME command-service COMMAND test-command-service)

    add_executable(test-control-server tests/test_control_server.cpp)
    target_link_libraries(test-control-server PRIVATE micropanel-touch-platform)
    target_compile_options(test-control-server PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME control-server COMMAND test-control-server)

    add_executable(test-privileged-broker tests/test_privileged_broker.cpp)
    target_link_libraries(test-privileged-broker PRIVATE micropanel-touch-platform)
    target_compile_options(test-privileged-broker PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME privileged-broker COMMAND test-privileged-broker)

    add_executable(test-privileged-broker-command-runner
        tests/test_privileged_broker_command_runner.cpp)
    target_link_libraries(test-privileged-broker-command-runner PRIVATE micropanel-touch-platform)
    target_compile_options(test-privileged-broker-command-runner PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME privileged-broker-command-runner COMMAND test-privileged-broker-command-runner)

    add_test(NAME privileged-entrypoint
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_privileged_entrypoint.sh"
                "$<TARGET_FILE:micropanel-touch-privileged>")
    set_tests_properties(privileged-entrypoint PROPERTIES SKIP_RETURN_CODE 77)

    # The A/B update engine and its suites live in
    # misc-tools/packages/pi-ab-update/tests and run from there.
    add_test(NAME update-health-hook
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_update_health_hook.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/packaging/micropanel-touch-update-health")

    add_executable(test-network-apply-service tests/test_network_apply_service.cpp)
    target_link_libraries(test-network-apply-service PRIVATE micropanel-touch-platform)
    target_compile_options(test-network-apply-service PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME network-apply-service COMMAND test-network-apply-service)

    add_executable(test-system-stats tests/test_system_stats.cpp)
    target_link_libraries(test-system-stats PRIVATE micropanel-touch-platform)
    target_compile_options(test-system-stats PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME system-stats COMMAND test-system-stats)

    add_executable(test-about-info tests/test_about_info.cpp)
    target_link_libraries(test-about-info PRIVATE micropanel-touch-platform)
    target_compile_options(test-about-info PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME about-info COMMAND test-about-info)

    add_executable(test-storage-health tests/test_storage_health.cpp)
    target_link_libraries(test-storage-health PRIVATE micropanel-touch-platform)
    target_compile_options(test-storage-health PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME storage-health COMMAND test-storage-health)

    add_executable(test-headless-ui tests/test_headless_ui.cpp)
    target_link_libraries(test-headless-ui PRIVATE micropanel-touch-ui)
    target_compile_options(test-headless-ui PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME headless-ui COMMAND test-headless-ui
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/config-basic.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/themes")

    # The same property in both boot geometries. Portrait is the accepted
    # bench mode; landscape is the other supported profile, and a screen that
    # only fits one of them is a screen that ships broken on the other.
    add_executable(test-menu-geometry tests/test_menu_geometry.cpp)
    target_link_libraries(test-menu-geometry PRIVATE micropanel-touch-ui)
    target_compile_options(test-menu-geometry PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME menu-geometry-portrait COMMAND test-menu-geometry
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/config-basic.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/themes" 320 480)
    add_test(NAME menu-geometry-landscape COMMAND test-menu-geometry
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/config-basic.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/themes" 480 320)

    add_executable(test-action-service tests/test_action_service.cpp)
    target_link_libraries(test-action-service PRIVATE micropanel-touch-platform)
    target_compile_options(test-action-service PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME action-service COMMAND test-action-service "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-simulated-flash"
                "[ERROR] simulated flash accepts no arguments")
    add_test(NAME static-ip-handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-network-static-ip"
                "[ERROR] static IPv4 handler requires interface address prefix gateway")
    add_test(NAME dhcp-handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-network-dhcp"
                "[ERROR] DHCP handler requires interface" no-arguments)
    add_test(NAME dhcp-server-handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-network-dhcp-server"
                "[ERROR] DHCP server handler requires interface address prefix lease-start lease-end")
    add_test(NAME power-handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-power"
                "[ERROR] power handler requires exactly one action" no-arguments)
    add_test(NAME power-handler-policy
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_power_handler_policy.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-power")
    add_test(NAME wifi-handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-wifi-join"
                "[ERROR] Wi-Fi join handler requires exactly one network name" no-arguments)
    add_test(NAME wifi-forget-handler-contract
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_handler_contract.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-wifi-forget"
                "[ERROR] Wi-Fi forget handler accepts no arguments")
    add_test(NAME wifi-handler-policy
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wifi_handler_policy.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-wifi-join"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-wifi-forget")
    add_test(NAME dhcp-server-handler-policy
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dhcp_server_handler_policy.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-network-static-ip"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-network-dhcp"
                "${CMAKE_CURRENT_SOURCE_DIR}/handlers/micropanel-touch-network-dhcp-server")

    add_test(NAME enable-piscreen-script
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enable_piscreen.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/enable-piscreen.sh")
    add_test(NAME enable-luckfox-ctp-script
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enable_luckfox_ctp.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/enable-luckfox-ctp.sh")
    add_test(NAME machine-identity-script
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_machine_identity_script.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/packaging/micropanel-touch-restore-machine-id")
endif()
