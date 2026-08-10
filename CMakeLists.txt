cmake_minimum_required(VERSION 3.21)

project(micropanel-touch VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

option(INSTALL_SYSTEMD_SERVICE "Install the micropanel-touch systemd unit" OFF)
set(MICROPANEL_TOUCH_SERVICE_USER "micropanel-touch" CACHE STRING
    "Appliance account used by the installed systemd service")
set(SYSTEMD_UNITFILE_ARGS "" CACHE STRING
    "Extra arguments passed to micropanel-touch by the installed systemd service")

find_package(Threads REQUIRED)
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
    src/core/StaticIpSettings.cpp
    src/core/UiEventQueue.cpp
)
target_include_directories(micropanel-touch-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(micropanel-touch-core PUBLIC cxx_std_17)
target_compile_options(micropanel-touch-core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(micropanel-touch-core PUBLIC Threads::Threads)

add_library(micropanel-touch-platform
    src/platform/DisplayBackend.cpp
    src/platform/NetworkInfo.cpp
    src/platform/TouchInput.cpp
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

add_executable(micropanel-touch
    src/main.cpp
    src/ui/HelloScreen.cpp
    src/ui/StarterUi.cpp
)
target_link_libraries(micropanel-touch PRIVATE micropanel-touch-platform micropanel-touch-starter-config)
target_compile_options(micropanel-touch PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS micropanel-touch RUNTIME DESTINATION usr/bin)
install(DIRECTORY screens/ DESTINATION share/micropanel-touch/screens)
install(FILES LICENSE README.md DESTINATION share/doc/micropanel-touch)
install(FILES packaging/sysusers.d/micropanel-touch.conf DESTINATION usr/lib/sysusers.d)
install(PROGRAMS tools/deploy.sh tools/bootstrap-pi.sh tools/enable-piscreen.sh
    tools/smoke-systemd-service.sh
    DESTINATION usr/share/micropanel-touch/tools)

if(INSTALL_SYSTEMD_SERVICE)
    configure_file(micropanel-touch.service.in micropanel-touch.service @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/micropanel-touch.service"
        DESTINATION lib/systemd/system)
endif()

if(BUILD_TESTING)
    add_executable(test-touch-mapper tests/test_touch_mapper.cpp)
    target_link_libraries(test-touch-mapper PRIVATE micropanel-touch-platform)
    target_compile_options(test-touch-mapper PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME touch-mapper COMMAND test-touch-mapper)

    add_executable(test-display-backend tests/test_display_backend.cpp)
    target_link_libraries(test-display-backend PRIVATE micropanel-touch-platform)
    target_compile_options(test-display-backend PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME display-backend COMMAND test-display-backend)

    add_executable(test-ui-event-queue tests/test_ui_event_queue.cpp)
    target_link_libraries(test-ui-event-queue PRIVATE micropanel-touch-core)
    target_compile_options(test-ui-event-queue PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ui-event-queue COMMAND test-ui-event-queue)

    add_executable(test-static-ip-settings tests/test_static_ip_settings.cpp)
    target_link_libraries(test-static-ip-settings PRIVATE micropanel-touch-core)
    target_compile_options(test-static-ip-settings PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME static-ip-settings COMMAND test-static-ip-settings)

    add_executable(test-starter-config tests/test_starter_config.cpp)
    target_link_libraries(test-starter-config PRIVATE micropanel-touch-starter-config)
    target_compile_options(test-starter-config PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME starter-config COMMAND test-starter-config
        "${CMAKE_CURRENT_SOURCE_DIR}/screens/config-basic.json")
endif()
