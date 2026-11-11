add_executable(InventoryIdentityTest PluginDeviceIdTest.cpp ${REPO_ROOT}/src/network/PluginDeviceId.cpp)
target_include_directories(InventoryIdentityTest PRIVATE stubs ${REPO_ROOT}/src)
target_link_libraries(InventoryIdentityTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(InventoryIdentityTest)
