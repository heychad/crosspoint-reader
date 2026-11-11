add_executable(ReferencePageMapTest
  ReferencePageMapTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ReferencePageMap.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)
target_include_directories(ReferencePageMapTest PRIVATE
  stubs
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)
target_link_libraries(ReferencePageMapTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReferencePageMapTest)
