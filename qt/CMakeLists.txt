# SPDX-License-Identifier: GPL-2.0-or-later
add_library(xastir_qt_ui STATIC
  map_projection.cpp map_projection.h
  tile_source.cpp tile_source.h
  map_widget.cpp map_widget.h
  map_menu.cpp map_menu.h
  vector_maps.cpp vector_maps.h
  main_window.cpp main_window.h
  legacy_menus.cpp legacy_menus.h
  message_service.cpp message_service.h
  message_ui.cpp message_ui.h
  packet_monitor.cpp packet_monitor.h
  station_navigator.cpp station_navigator.h
  station_menu.cpp station_menu.h
  station_details.cpp station_details.h
  interface_control.cpp interface_control.h
  aprsis_client.cpp aprsis_client.h
  beacon_control.cpp beacon_control.h
  history_store.cpp history_store.h
  history_ui.cpp history_ui.h
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
set(offline_map_files "${PROJECT_SOURCE_DIR}/worldhi.map" "${PROJECT_SOURCE_DIR}/NaturalEarthVector/LICENSE.md")
if(SHAPELIB_INCLUDE_DIR AND SHAPELIB_LIBRARY AND XASTIR_WITH_SHAPELIB)
  foreach(layer IN ITEMS admin_0_countries_lakes coastline lakes rivers_lake_centerlines)
    foreach(extension IN ITEMS shp shx dbf)
      list(APPEND offline_map_files "${PROJECT_SOURCE_DIR}/NaturalEarthVector/ne_50m_${layer}.${extension}")
    endforeach()
  endforeach()
endif()
qt_add_resources(xastir_qt_ui offline_maps PREFIX "/maps" BASE "${PROJECT_SOURCE_DIR}" FILES ${offline_map_files})
target_include_directories(xastir_qt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xastir_qt_ui PUBLIC Qt6::Widgets Qt6::Network SQLite::SQLite3 xastir_core)
if(MSVC)
  target_compile_options(xastir_qt_ui PRIVATE /W4)
else()
  target_compile_options(xastir_qt_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(xastir-qt WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(xastir-qt PRIVATE xastir_qt_ui)
set_target_properties(xastir-qt PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER org.xastir.qt
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
install(TARGETS xastir-qt RUNTIME DESTINATION bin BUNDLE DESTINATION .)

if(BUILD_TESTING)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
  qt_add_executable(test_history tests/test_history.cpp)
  target_link_libraries(test_history PRIVATE xastir_qt_ui Qt6::Test)
  set_target_properties(test_history PROPERTIES WIN32_EXECUTABLE FALSE)
  if(TARGET Qt6::QOffscreenIntegrationPlugin)
    qt_import_plugins(test_history INCLUDE Qt6::QOffscreenIntegrationPlugin)
  endif()
  add_test(NAME history COMMAND test_history)
  set_tests_properties(history PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)
  qt_add_executable(test_connections tests/test_connections.cpp)
  target_link_libraries(test_connections PRIVATE xastir_qt_ui Qt6::Test)
  set_target_properties(test_connections PROPERTIES WIN32_EXECUTABLE FALSE)
  if(TARGET Qt6::QOffscreenIntegrationPlugin)
    qt_import_plugins(test_connections INCLUDE Qt6::QOffscreenIntegrationPlugin)
  endif()
  add_test(NAME connections COMMAND test_connections)
  set_tests_properties(connections PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)
  qt_add_executable(test_qt_map tests/test_qt_map.cpp)
  target_link_libraries(test_qt_map PRIVATE xastir_qt_ui Qt6::Test)
  configure_file(examples/stations.json examples/stations.json COPYONLY)
  target_compile_definitions(test_qt_map PRIVATE
    STATION_EXAMPLE_FILE="examples/stations.json"
    VECTOR_DATA_DIR="${PROJECT_SOURCE_DIR}"
  )
  qt_add_executable(test_agwpe tests/test_agwpe.cpp)
  target_link_libraries(test_agwpe PRIVATE xastir_qt_ui Qt6::Test)
  qt_add_executable(test_messages tests/test_messages.cpp)
  target_link_libraries(test_messages PRIVATE xastir_qt_ui Qt6::Test)
  add_test(NAME messages COMMAND test_messages)
  qt_add_executable(test_packet_monitor tests/test_packet_monitor.cpp)
  target_link_libraries(test_packet_monitor PRIVATE xastir_qt_ui Qt6::Test)
  add_test(NAME packet_monitor COMMAND test_packet_monitor)
  qt_add_executable(test_station_navigator tests/test_station_navigator.cpp)
  target_link_libraries(test_station_navigator PRIVATE xastir_qt_ui Qt6::Test)
  add_test(NAME station_navigator COMMAND test_station_navigator)
  qt_add_executable(test_station_menu tests/test_station_menu.cpp)
  target_link_libraries(test_station_menu PRIVATE xastir_qt_ui Qt6::Test)
  foreach(test_target IN ITEMS test_qt_map test_agwpe test_messages
      test_packet_monitor test_station_navigator test_station_menu)
    # Console test executables let CTest capture failures on Windows.
    set_target_properties(${test_target} PROPERTIES WIN32_EXECUTABLE FALSE)
  endforeach()
  # Static Qt imports only the default Windows platform plugin. Keep the
  # headless backend in test executables for CI and cross-build validation.
  if(TARGET Qt6::QOffscreenIntegrationPlugin)
    foreach(test_target IN ITEMS test_qt_map test_agwpe test_messages
        test_packet_monitor test_station_navigator test_station_menu)
      qt_import_plugins(${test_target} INCLUDE Qt6::QOffscreenIntegrationPlugin)
    endforeach()
  endif()
  add_test(NAME station_menu COMMAND test_station_menu)
  set_tests_properties(station_menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  set_tests_properties(station_navigator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  set_tests_properties(packet_monitor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  set_tests_properties(messages PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  add_test(NAME agwpe COMMAND test_agwpe)
  set_tests_properties(agwpe PROPERTIES TIMEOUT 30)
  add_test(NAME qt_map COMMAND test_qt_map)
  set_tests_properties(qt_map PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
  )
endif()
