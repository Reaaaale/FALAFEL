cmake_minimum_required(VERSION 3.24)

project(falafel_simulator LANGUAGES C)

include(FetchContent)

find_package(SDL2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(../cmake/simulator_messaging.cmake)
set(FALAFEL_CHARACTER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/falafel_character.c")
add_custom_command(OUTPUT "${FALAFEL_CHARACTER_SOURCE}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_simulator_asset.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/falafel.png" "${FALAFEL_CHARACTER_SOURCE}"
    DEPENDS assets/falafel.png ../tools/embed_simulator_asset.py
    VERBATIM)

set(FALAFEL_ROTATION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/falafel_rotation.c")
set(FALAFEL_ROTATION_PNGS)
foreach(frame RANGE 0 23)
    if(frame LESS 10)
        set(frame_name "00${frame}")
    else()
        set(frame_name "0${frame}")
    endif()
    list(APPEND FALAFEL_ROTATION_PNGS "assets/falafel_rotation/frame_${frame_name}.png")
endforeach()
add_custom_command(OUTPUT "${FALAFEL_ROTATION_SOURCE}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_simulator_asset.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/falafel_rotation" "${FALAFEL_ROTATION_SOURCE}"
    DEPENDS ${FALAFEL_ROTATION_PNGS} ../tools/embed_simulator_asset.py
    VERBATIM)


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
    ../shared/gui/falafel_gui.c
    "${FALAFEL_CHARACTER_SOURCE}"
    "${FALAFEL_ROTATION_SOURCE}"
    src/ui_smoke.c
    ../shared/gui/falafel_theme.c
    ../shared/gui/falafel_status_bar.c
    ../shared/gui/falafel_home.c
    ../shared/gui/falafel_messages.c
    ../shared/gui/falafel_composer.c
)

target_include_directories(falafel_simulator PRIVATE
    ../shared/include
    config
    ../shared/gui
)

target_compile_features(falafel_simulator PRIVATE c_std_11)
target_compile_options(falafel_simulator PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(falafel_simulator PRIVATE
    simulator_messaging
    lvgl::lvgl
    SDL2::SDL2
    Threads::Threads
)

enable_testing()
add_test(NAME circular_gui_smoke COMMAND falafel_simulator --device 41 --peer 42
    --ui-smoke-test)
set_tests_properties(circular_gui_smoke PROPERTIES TIMEOUT 20)

# Reuse the explicit fixture provisioner; the simulator never provisions keys.
add_executable(falafel_mailbox_fixture ../experiments/desktop-mailbox/mailbox_peer.c)
target_link_libraries(falafel_mailbox_fixture PRIVATE simulator_messaging)
add_executable(messaging_probe ../test/messaging_probe.c)
target_link_libraries(messaging_probe PRIVATE simulator_messaging)
target_compile_options(messaging_probe PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_executable(test_history ../test/test_history.c)
target_link_libraries(test_history PRIVATE simulator_messaging)
target_compile_options(test_history PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME encrypted_history COMMAND test_history)
set_tests_properties(encrypted_history PROPERTIES TIMEOUT 90)
add_test(NAME messaging_controller_integration COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/test_simulator_messaging.py
    $<TARGET_FILE:falafel_simulator> $<TARGET_FILE:falafel_mailbox_fixture>
    $<TARGET_FILE:messaging_probe> controller)
add_test(NAME simulator_e2ee_integration COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../test/test_simulator_messaging.py
    $<TARGET_FILE:falafel_simulator> $<TARGET_FILE:falafel_mailbox_fixture>
    $<TARGET_FILE:messaging_probe> gui)
set_tests_properties(messaging_controller_integration simulator_e2ee_integration
    PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
