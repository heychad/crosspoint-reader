FetchContent_GetProperties(ArduinoJson)
add_executable(QuickBindingsTest QuickBindingsTest.cpp)
target_include_directories(QuickBindingsTest PRIVATE stubs ../../src ${arduinojson_SOURCE_DIR}/src)
target_link_libraries(QuickBindingsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QuickBindingsTest)

add_test(NAME QuickActionsUI COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui.py ${arduinojson_SOURCE_DIR}/src)
add_test(NAME QuickActionsWeb COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_web.py ${arduinojson_SOURCE_DIR}/src)
