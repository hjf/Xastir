# SPDX-License-Identifier: GPL-2.0-or-later
add_library(xastir_qt_ui STATIC
  map_projection.cpp map_projection.h
  tile_source.cpp tile_source.h
  map_widget.cpp map_widget.h
  main_window.cpp main_window.h
  legacy_menus.cpp legacy_menus.h
  message_service.cpp message_service.h
  message_ui.cpp message_ui.h
  aprs_symbols.cpp aprs_symbols.h
  station_model.cpp station_model.h
  xastir_config.cpp xastir_config.h
  aprs_decoder.cpp aprs_decoder.h
  agwpe_client.cpp agwpe_client.h
)
qt_add_resources(xastir_qt_ui aprs_symbols
  PREFIX "/aprs"
  BASE "${PROJECT_SOURCE_DIR}/symbols"
  FILES "${PROJECT_SOURCE_DIR}/symbols/symbols.dat"
)
qt_add_resources(xastir_qt_ui legacy_menus
  PREFIX "/menus"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/legacy_menus.json"
)
target_include_directories(xastir_qt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xastir_qt_ui PUBLIC Qt6::Widgets Qt6::Network xastir_core)
if(MSVC)
  target_compile_options(xastir_qt_ui PRIVATE /W4)
else()
  target_compile_options(xastir_qt_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(xastir-qt WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(xastir-qt PRIVATE xastir_qt_ui)
set_target_properties(xastir-qt PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER org.xastir.qt
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
install(TARGETS xastir-qt RUNTIME DESTINATION bin BUNDLE DESTINATION .)

if(BUILD_TESTING)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
  add_executable(test_qt_map tests/test_qt_map.cpp)
  target_link_libraries(test_qt_map PRIVATE xastir_qt_ui Qt6::Test)
  target_compile_definitions(test_qt_map PRIVATE
    STATION_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/examples/stations.json"
  )
  add_executable(test_agwpe tests/test_agwpe.cpp)
  target_link_libraries(test_agwpe PRIVATE xastir_qt_ui Qt6::Test)
  add_executable(test_messages tests/test_messages.cpp)
  target_link_libraries(test_messages PRIVATE xastir_qt_ui Qt6::Test)
  add_test(NAME messages COMMAND test_messages)
  set_tests_properties(messages PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  add_test(NAME agwpe COMMAND test_agwpe)
  set_tests_properties(agwpe PROPERTIES TIMEOUT 30)
  add_test(NAME qt_map COMMAND test_qt_map)
  set_tests_properties(qt_map PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
  )
endif()
