add_executable(PersonalUpdateVersionTest
  PersonalUpdateVersionTest.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateVersion.cpp
)
target_include_directories(PersonalUpdateVersionTest PRIVATE ${REPO_ROOT}/src/network)
target_link_libraries(PersonalUpdateVersionTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PersonalUpdateVersionTest)
