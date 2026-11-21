cmake_minimum_required(VERSION 3.20)

project(ql32 VERSION 0.1.0 LANGUAGES CXX)

option(QL32_BUILD_TESTS "Build QL/32 host tests" ON)
option(QL32_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(QL32_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "QL32_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(ql32_portable
  src/music/ay38912.cpp
  src/music/lh5.cpp
  src/music/vtx.cpp
  src/music/vtx_player.cpp
  src/music/ym.cpp
  src/music/ym_player.cpp
  src/music/pt3.cpp
  src/music/pt3_tables.cpp
  src/music/sqt.cpp
  src/language/interpreter.cpp
  src/graphics/bitmap.cpp
  src/graphics/font.cpp
  src/graphics/window_manager.cpp
  src/graphics/windowed_service.cpp
  src/runtime/channel_registry.cpp
  src/runtime/buffer.cpp
  src/runtime/ui_service.cpp
  src/ui/graphic_presenter.cpp
  src/ui/line_editor.cpp
  src/runtime/ymodem.cpp
  src/vm/interpreter.cpp
  src/vm/scheduler.cpp
)
target_include_directories(ql32_portable PUBLIC include)
target_compile_features(ql32_portable PUBLIC cxx_std_17)

add_library(ql32_host_adapters
  src/platform/linux/directory_channels.cpp
  src/platform/linux/virtual_hardware.cpp
  src/platform/linux/directory_storage.cpp
  src/platform/linux/console_ui.cpp
  src/platform/linux/system_clock.cpp
  src/platform/linux/memory.cpp
  src/platform/linux/svg_graphics.cpp
  src/platform/linux/ppm_presenter.cpp
  src/platform/esp32/remote_debug_protocol.cpp
)
target_link_libraries(ql32_host_adapters PUBLIC ql32_portable)

# Host applications and integration tests use the portable runtime plus its
# host service adapters. Embedded builds link ql32_portable with board adapters.
add_library(ql32_host INTERFACE)
target_link_libraries(ql32_host INTERFACE ql32_portable ql32_host_adapters)

add_executable(ql32 src/main.cpp)
target_link_libraries(ql32 PRIVATE ql32_host)

if(QL32_BUILD_TESTS)
  enable_testing()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME ql32_font_converter_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/font_converter_tests.py)
    add_test(NAME ql32_qimg_converter_tests
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/ql32_remote_debugger/tests/test_qimg.py)
    add_test(NAME ql32_olimex_profile_config_tests
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/olimex_profile_config_tests.py)
  endif()
  function(add_cli_conformance_test name)
    add_test(
      NAME ${name}
      COMMAND ${CMAKE_COMMAND}
        -DQL32_EXECUTABLE=$<TARGET_FILE:ql32>
        -DTEST_NAME=${name}
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/${name}.bas
        -DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/${name}.out
        -DEXPECTED_STDERR_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/${name}.err
        -DEXPECTED_EXIT_CODE=${ARGN}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake
    )
  endfunction()

  add_cli_conformance_test(cli_stdin 0)
  add_cli_conformance_test(cli_pause 0)
  add_cli_conformance_test(cli_file 0)
  add_cli_conformance_test(cli_language_slice 0)
  add_cli_conformance_test(cli_expression_rules 0)
  add_cli_conformance_test(cli_numeric_portable 0)
  add_cli_conformance_test(cli_print_separators 0)
  add_cli_conformance_test(cli_block_if 0)
  add_cli_conformance_test(cli_if_type_error 1)
  add_cli_conformance_test(cli_for_ranges 0)
  add_cli_conformance_test(cli_for_inline 0)
  add_cli_conformance_test(cli_for_zero_step 1)
  add_cli_conformance_test(cli_repeat_control 0)
  add_cli_conformance_test(cli_repeat_inline 0)
  add_cli_conformance_test(cli_for_exit 0)
  add_cli_conformance_test(cli_functions 0)
  add_cli_conformance_test(cli_call_depth 1)
  add_cli_conformance_test(cli_procedure_locals 0)
  add_cli_conformance_test(cli_arrays 0)
  add_cli_conformance_test(cli_array_bounds 1)
  add_cli_conformance_test(cli_array_limit 1)
  add_cli_conformance_test(cli_select 0)
  add_cli_conformance_test(cli_program_commands 0)
  add_cli_conformance_test(cli_abbreviations 0)
  add_cli_conformance_test(cli_array_by_reference 0)
  add_cli_conformance_test(cli_select_edges 0)
  add_test(
    NAME ql32_golden_corpus
    COMMAND ${CMAKE_COMMAND}
      -DQL32_EXECUTABLE=$<TARGET_FILE:ql32>
      -DQL32_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_corpus_test.cmake
  )
  add_test(
    NAME cli_ui_dialog_menu
    COMMAND ${CMAKE_COMMAND}
      -DQL32_EXECUTABLE=$<TARGET_FILE:ql32>
      -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/ui.bas
      -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/ui_input.txt
      -DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/ui_expected.out
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_ui_test.cmake
  )
  add_executable(ql32_runtime_tests tests/runtime_tests.cpp)
  target_link_libraries(ql32_runtime_tests PRIVATE ql32_host)
  add_test(NAME ql32_runtime_tests COMMAND ql32_runtime_tests)
  add_executable(ql32_music_tests tests/music_tests.cpp)
  target_link_libraries(ql32_music_tests PRIVATE ql32_host)
  target_compile_definitions(ql32_music_tests PRIVATE QL32_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
  add_test(NAME ql32_music_tests COMMAND ql32_music_tests)
  add_executable(ql32_scheduler_tests tests/scheduler_tests.cpp)
  target_link_libraries(ql32_scheduler_tests PRIVATE ql32_host)
  add_test(NAME ql32_scheduler_tests COMMAND ql32_scheduler_tests)
  add_executable(ql32_debugger_tests tests/debugger_tests.cpp)
  target_link_libraries(ql32_debugger_tests PRIVATE ql32_host)
  add_test(NAME ql32_debugger_tests COMMAND ql32_debugger_tests)
  add_executable(ql32_remote_debug_protocol_tests tests/remote_debug_protocol_tests.cpp)
  target_link_libraries(ql32_remote_debug_protocol_tests PRIVATE ql32_host)
  add_test(NAME ql32_remote_debug_protocol_tests COMMAND ql32_remote_debug_protocol_tests)
  add_executable(ql32_storage_tests tests/storage_tests.cpp)
  target_link_libraries(ql32_storage_tests PRIVATE ql32_host)
  add_test(NAME ql32_storage_tests COMMAND ql32_storage_tests)
  add_executable(ql32_ymodem_tests tests/ymodem_tests.cpp)
  target_link_libraries(ql32_ymodem_tests PRIVATE ql32_host)
  add_test(NAME ql32_ymodem_tests COMMAND ql32_ymodem_tests)
  add_executable(ql32_channel_tests tests/channel_tests.cpp)
  target_link_libraries(ql32_channel_tests PRIVATE ql32_host)
  add_test(NAME ql32_channel_tests COMMAND ql32_channel_tests)
  add_executable(ql32_hardware_tests tests/hardware_tests.cpp)
  target_link_libraries(ql32_hardware_tests PRIVATE ql32_host)
  add_test(NAME ql32_hardware_tests COMMAND ql32_hardware_tests)
  add_executable(ql32_pin_conflict_tests tests/pin_conflict_tests.cpp)
  target_link_libraries(ql32_pin_conflict_tests PRIVATE ql32_host)
  add_test(NAME ql32_pin_conflict_tests COMMAND ql32_pin_conflict_tests)
  add_test(
    NAME ql32_hardware_repl
    COMMAND ${CMAKE_COMMAND}
      -DQL32_EXECUTABLE=$<TARGET_FILE:ql32>
      -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/hardware_repl.in
      -DEXPECTED_STDOUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/hardware_repl.out
      -DWORKING_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_repl_test.cmake
  )
  add_executable(ql32_ui_service_tests tests/ui_service_tests.cpp)
  target_link_libraries(ql32_ui_service_tests PRIVATE ql32_host)
  add_test(NAME ql32_ui_service_tests COMMAND ql32_ui_service_tests)
  add_executable(ql32_line_editor_tests tests/line_editor_tests.cpp)
  target_link_libraries(ql32_line_editor_tests PRIVATE ql32_portable)
  add_test(NAME ql32_line_editor_tests COMMAND ql32_line_editor_tests)
  add_executable(ql32_line_damage_tests tests/line_damage_tests.cpp)
  target_link_libraries(ql32_line_damage_tests PRIVATE ql32_portable)
  add_test(NAME ql32_line_damage_tests COMMAND ql32_line_damage_tests)
  add_executable(ql32_graphic_ui_tests tests/graphic_ui_tests.cpp)
  target_link_libraries(ql32_graphic_ui_tests PRIVATE ql32_host)
  add_test(NAME ql32_graphic_ui_tests COMMAND ql32_graphic_ui_tests)
  add_executable(ql32_graphics_tests tests/graphics_tests.cpp)
  target_link_libraries(ql32_graphics_tests PRIVATE ql32_host)
  add_test(NAME ql32_graphics_tests COMMAND ql32_graphics_tests)
  add_executable(ql32_olimex_framebuffer_tests tests/olimex_framebuffer_tests.cpp)
  target_link_libraries(ql32_olimex_framebuffer_tests PRIVATE ql32_portable)
  add_test(NAME ql32_olimex_framebuffer_tests COMMAND ql32_olimex_framebuffer_tests)
  find_package(Threads REQUIRED)
  add_executable(ql32_olimex_single_framebuffer_tests tests/olimex_single_framebuffer_tests.cpp)
  target_link_libraries(ql32_olimex_single_framebuffer_tests PRIVATE ql32_portable Threads::Threads)
  target_compile_definitions(ql32_olimex_single_framebuffer_tests PRIVATE QL32_PACKED4_SHARED_WRITES)
  add_test(NAME ql32_olimex_single_framebuffer_tests COMMAND ql32_olimex_single_framebuffer_tests)
  add_executable(ql32_olimex_scanout_geometry_tests tests/olimex_scanout_geometry_tests.cpp)
  target_link_libraries(ql32_olimex_scanout_geometry_tests PRIVATE ql32_portable)
  add_test(NAME ql32_olimex_scanout_geometry_tests COMMAND ql32_olimex_scanout_geometry_tests)
  add_executable(ql32_olimex_present_stats_tests tests/olimex_present_stats_tests.cpp)
  target_link_libraries(ql32_olimex_present_stats_tests PRIVATE ql32_portable)
  add_test(NAME ql32_olimex_present_stats_tests COMMAND ql32_olimex_present_stats_tests)
  add_executable(ql32_olimex_switchable_modes_tests tests/olimex_switchable_display_modes_tests.cpp)
  target_link_libraries(ql32_olimex_switchable_modes_tests PRIVATE ql32_portable)
  add_test(NAME ql32_olimex_switchable_modes_tests COMMAND ql32_olimex_switchable_modes_tests)
  foreach(mode IN ITEMS default 320X240 400X300 640X400 640X480)
    set(mode_test ql32_olimex_fixed_mode_${mode}_tests)
    add_executable(${mode_test} tests/olimex_fixed_display_modes_tests.cpp)
    target_link_libraries(${mode_test} PRIVATE ql32_portable)
    if(NOT mode STREQUAL "default")
      target_compile_definitions(${mode_test} PRIVATE QL32_OLIMEX_DMA_TEST_MODE_${mode})
    endif()
    add_test(NAME ${mode_test} COMMAND ${mode_test})
  endforeach()
  add_executable(ql32_olimex_canvas_raster_tests
    tests/olimex_canvas_raster_tests.cpp
    src/platform/olimex_vga/canvas_raster_ops.cpp
  )
  target_link_libraries(ql32_olimex_canvas_raster_tests PRIVATE ql32_portable)
  add_test(NAME ql32_olimex_canvas_raster_tests COMMAND ql32_olimex_canvas_raster_tests)
  add_executable(ql32_capabilities_tests tests/capabilities_tests.cpp)
  target_link_libraries(ql32_capabilities_tests PRIVATE ql32_host)
  add_test(NAME ql32_capabilities_tests COMMAND ql32_capabilities_tests)
  add_executable(ql32_clock_service_tests tests/clock_service_tests.cpp)
  target_link_libraries(ql32_clock_service_tests PRIVATE ql32_host)
  add_test(NAME ql32_clock_service_tests COMMAND ql32_clock_service_tests)
  add_executable(ql32_esp32_clock_tests
    tests/esp32_clock_tests.cpp
    src/platform/esp32/clock.cpp
  )
  target_include_directories(ql32_esp32_clock_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_esp32_clock_tests PRIVATE cxx_std_20)
  add_test(NAME ql32_esp32_clock_tests COMMAND ql32_esp32_clock_tests)
  add_executable(ql32_esp32_network_tests
    tests/esp32_network_tests.cpp
    src/platform/esp32/network.cpp
  )
  target_include_directories(ql32_esp32_network_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_esp32_network_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_esp32_network_tests COMMAND ql32_esp32_network_tests)
  add_executable(ql32_network_service_tests tests/network_service_tests.cpp)
  target_link_libraries(ql32_network_service_tests PRIVATE ql32_host)
  add_test(NAME ql32_network_service_tests COMMAND ql32_network_service_tests)
  add_executable(ql32_include_tests tests/include_tests.cpp)
  target_link_libraries(ql32_include_tests PRIVATE ql32_host)
  add_test(NAME ql32_include_tests COMMAND ql32_include_tests)
  add_executable(ql32_esp32_storage_tests
    tests/esp32_storage_tests.cpp
    src/platform/esp32/sd_storage.cpp
  )
  target_include_directories(ql32_esp32_storage_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_esp32_storage_tests PRIVATE cxx_std_20)
  add_test(NAME ql32_esp32_storage_tests COMMAND ql32_esp32_storage_tests)

  add_executable(ql32_esp32_channels_tests
    tests/esp32_channels_tests.cpp
    src/platform/esp32/sd_channels.cpp
  )
  target_include_directories(ql32_esp32_channels_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_esp32_channels_tests PRIVATE cxx_std_20)
  add_test(NAME ql32_esp32_channels_tests COMMAND ql32_esp32_channels_tests)
  add_executable(ql32_drive_router_tests
    tests/drive_router_tests.cpp
    src/platform/esp32/drive_router.cpp
  )
  target_include_directories(ql32_drive_router_tests PRIVATE include)
  target_compile_features(ql32_drive_router_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_drive_router_tests COMMAND ql32_drive_router_tests)
  add_executable(ql32_lilygo_vga32_profile_tests tests/lilygo_vga32_profile_tests.cpp)
  target_include_directories(ql32_lilygo_vga32_profile_tests PRIVATE boards/lilygo_vga32 include)
  target_compile_features(ql32_lilygo_vga32_profile_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_lilygo_vga32_profile_tests COMMAND ql32_lilygo_vga32_profile_tests)

  add_executable(ql32_olimex_esp32_sbc_fabgl_profile_tests
    tests/olimex_esp32_sbc_fabgl_profile_tests.cpp
  )
  target_include_directories(ql32_olimex_esp32_sbc_fabgl_profile_tests PRIVATE
    boards/olimex_esp32_sbc_fabgl include)
  target_compile_features(ql32_olimex_esp32_sbc_fabgl_profile_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_olimex_esp32_sbc_fabgl_profile_tests
    COMMAND ql32_olimex_esp32_sbc_fabgl_profile_tests)

  add_executable(ql32_esp32_2432s028_profile_tests
    tests/esp32_2432s028_profile_tests.cpp
  )
  target_include_directories(ql32_esp32_2432s028_profile_tests PRIVATE
    boards/esp32_2432s028 include)
  target_compile_features(ql32_esp32_2432s028_profile_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_esp32_2432s028_profile_tests COMMAND ql32_esp32_2432s028_profile_tests)

  add_executable(ql32_tenstar_esp32s3_profile_tests
    tests/tenstar_esp32s3_profile_tests.cpp
  )
  target_include_directories(ql32_tenstar_esp32s3_profile_tests PRIVATE
    boards/ql32_tenstar_esp32s3 include)
  target_compile_features(ql32_tenstar_esp32s3_profile_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_tenstar_esp32s3_profile_tests COMMAND ql32_tenstar_esp32s3_profile_tests)

  add_executable(ql32_tenstar_esp32s3_graphics_tests
    tests/tenstar_esp32s3_graphics_tests.cpp
    src/platform/tenstar_esp32s3/graphics.cpp
    src/platform/tenstar_esp32s3/frame_presenter.cpp
  )
  target_include_directories(ql32_tenstar_esp32s3_graphics_tests PRIVATE
    tests/fakes include)
  target_compile_features(ql32_tenstar_esp32s3_graphics_tests PRIVATE cxx_std_17)
  target_link_libraries(ql32_tenstar_esp32s3_graphics_tests PRIVATE ql32_portable)
  add_test(NAME ql32_tenstar_esp32s3_graphics_tests
    COMMAND ql32_tenstar_esp32s3_graphics_tests)

  add_executable(ql32_tenstar_esp32s3_console_tests
    tests/tenstar_esp32s3_console_tests.cpp
    src/platform/tenstar_esp32s3/text_console.cpp
  )
  target_include_directories(ql32_tenstar_esp32s3_console_tests PRIVATE
    tests/fakes include)
  target_compile_features(ql32_tenstar_esp32s3_console_tests PRIVATE cxx_std_17)
  target_link_libraries(ql32_tenstar_esp32s3_console_tests PRIVATE ql32_portable)
  add_test(NAME ql32_tenstar_esp32s3_console_tests
    COMMAND ql32_tenstar_esp32s3_console_tests)

  add_executable(ql32_esp32_cyd_graphics_tests
    tests/esp32_cyd_graphics_tests.cpp
    src/platform/esp32_cyd/graphics.cpp
    src/platform/esp32_cyd/frame_presenter.cpp
  )
  target_include_directories(ql32_esp32_cyd_graphics_tests PRIVATE
    tests/fakes include)
  target_compile_features(ql32_esp32_cyd_graphics_tests PRIVATE cxx_std_17)
  target_link_libraries(ql32_esp32_cyd_graphics_tests PRIVATE ql32_portable)
  add_test(NAME ql32_esp32_cyd_graphics_tests COMMAND ql32_esp32_cyd_graphics_tests)

  add_executable(ql32_esp32_cyd_console_tests
    tests/esp32_cyd_console_tests.cpp
    src/platform/esp32_cyd/text_console.cpp
  )
  target_include_directories(ql32_esp32_cyd_console_tests PRIVATE
    tests/fakes include)
  target_compile_features(ql32_esp32_cyd_console_tests PRIVATE cxx_std_17)
  target_link_libraries(ql32_esp32_cyd_console_tests PRIVATE ql32_portable)
  add_test(NAME ql32_esp32_cyd_console_tests COMMAND ql32_esp32_cyd_console_tests)

  add_executable(ql32_line_ui_presenter_tests
    tests/line_ui_presenter_tests.cpp
    src/platform/esp32/line_ui_presenter.cpp
  )
  target_link_libraries(ql32_line_ui_presenter_tests PRIVATE ql32_host)
  target_include_directories(ql32_line_ui_presenter_tests PRIVATE include)
  target_compile_features(ql32_line_ui_presenter_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_line_ui_presenter_tests COMMAND ql32_line_ui_presenter_tests)
  add_executable(ql32_linux_graphics_tests tests/linux_graphics_tests.cpp)
  target_link_libraries(ql32_linux_graphics_tests PRIVATE ql32_host)
  add_test(NAME ql32_linux_graphics_tests COMMAND ql32_linux_graphics_tests)
  add_executable(ql32_fabgl_graphics_tests
    tests/fabgl_graphics_tests.cpp
    src/platform/fabgl_vga/graphics.cpp
    src/platform/fabgl_vga/frame_presenter.cpp
  )
  target_include_directories(ql32_fabgl_graphics_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_fabgl_graphics_tests PRIVATE cxx_std_20)
  add_test(NAME ql32_fabgl_graphics_tests COMMAND ql32_fabgl_graphics_tests)
  add_executable(ql32_fabgl_display_modes_tests
    tests/fabgl_display_modes_tests.cpp
    src/platform/fabgl_vga/display_modes.cpp
  )
  target_include_directories(ql32_fabgl_display_modes_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_fabgl_display_modes_tests PRIVATE cxx_std_17)
  add_test(NAME ql32_fabgl_display_modes_tests COMMAND ql32_fabgl_display_modes_tests)
  add_executable(ql32_fabgl_presenter_tests
    tests/fabgl_presenter_tests.cpp
    src/platform/fabgl_vga/frame_presenter.cpp
  )
  target_include_directories(ql32_fabgl_presenter_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_fabgl_presenter_tests PRIVATE cxx_std_20)
  add_test(NAME ql32_fabgl_presenter_tests COMMAND ql32_fabgl_presenter_tests)
  add_executable(ql32_fabgl_keyboard_tests
    tests/fabgl_keyboard_tests.cpp
    src/platform/fabgl_vga/keyboard.cpp
  )
  target_include_directories(ql32_fabgl_keyboard_tests PRIVATE tests/fakes include)
  target_compile_features(ql32_fabgl_keyboard_tests PRIVATE cxx_std_20)
  add_test(NAME ql32_fabgl_keyboard_tests COMMAND ql32_fabgl_keyboard_tests)
  add_cli_conformance_test(cli_mixed_type_error 1)
  add_cli_conformance_test(cli_div_overflow 1)
  add_cli_conformance_test(cli_invalid_power 1)
  add_cli_conformance_test(cli_syntax_error 1)
  add_cli_conformance_test(cli_runtime_error 1)
endif()

