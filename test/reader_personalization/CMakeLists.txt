FetchContent_Declare(ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ArduinoJson)
add_executable(ReaderPersonalizationTest ReaderPersonalizationTest.cpp CompletionPromptTest.cpp)
target_include_directories(ReaderPersonalizationTest PRIVATE ../../src ${arduinojson_SOURCE_DIR}/src)
target_link_libraries(ReaderPersonalizationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderPersonalizationTest)
