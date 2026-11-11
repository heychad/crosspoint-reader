add_executable(DeviceSecretTest DeviceSecretTest.cpp ${REPO_ROOT}/lib/DeviceSecret/DeviceSecret.cpp)
target_include_directories(DeviceSecretTest PRIVATE stubs ${REPO_ROOT}/lib/DeviceSecret)
target_link_libraries(DeviceSecretTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DeviceSecretTest)

add_executable(DeviceSecretExistingTest DeviceSecretExistingTest.cpp ${REPO_ROOT}/lib/DeviceSecret/DeviceSecret.cpp)
target_include_directories(DeviceSecretExistingTest PRIVATE stubs ${REPO_ROOT}/lib/DeviceSecret)
target_link_libraries(DeviceSecretExistingTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DeviceSecretExistingTest)
