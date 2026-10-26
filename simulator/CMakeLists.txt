cmake_minimum_required(VERSION 3.24)

project(falafel_simulator LANGUAGES C)

include(FetchContent)

find_package(SDL2 CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config/lv_conf.h"
    CACHE STRING "Falafel LVGL configuration" FORCE)
set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "Use falafel lv_conf.h" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Do not build LVGL demos" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Do not build LVGL examples" FORCE)

FetchContent_Declare(
    lvgl
    URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.4.0.tar.gz
    URL_HASH SHA256=932c864de98431b38c5758a87f1de12bbee7b76943b8312bcf13d2f29ac14627
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(lvgl)

# LVGL's SDL driver is part of the LVGL target, so it also needs SDL headers.
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

add_executable(falafel_simulator
    src/main.c
    src/trackpad_navigation.c
    src/desktop_transport.c
    ../shared/app/falafel_app.c
    ../shared/app/falafel_protocol.c
    ../shared/gui/falafel_gui.c
    ../shared/gui/falafel_assets.c
    ../shared/gui/falafel_theme.c
    ../shared/gui/falafel_status_bar.c
    ../shared/gui/falafel_home.c
    ../shared/gui/falafel_messages.c
    ../shared/gui/falafel_composer.c
)

target_include_directories(falafel_simulator PRIVATE
    ../shared/include
    config
)

target_compile_features(falafel_simulator PRIVATE c_std_11)
target_compile_options(falafel_simulator PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(falafel_simulator PRIVATE
    lvgl::lvgl
    SDL2::SDL2
    Threads::Threads
)
