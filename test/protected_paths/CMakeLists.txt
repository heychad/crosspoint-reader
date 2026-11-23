add_executable(ProtectedPathsTest ProtectedPathsTest.cpp)
target_compile_definitions(ProtectedPathsTest PRIVATE FREEINK_DEVICE_X4PRO=1)

target_include_directories(ProtectedPathsTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ProtectedPathsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProtectedPathsTest)
