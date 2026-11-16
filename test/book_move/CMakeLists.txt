add_executable(BookMovePlanTest BookMovePlanTest.cpp ${REPO_ROOT}/src/util/BookmarkUtil.cpp)
target_include_directories(BookMovePlanTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/hal ${REPO_ROOT}/lib/Serialization)
target_link_libraries(BookMovePlanTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookMovePlanTest)
