add_executable(QuickLockStateTest QuickLockStateTest.cpp)
target_include_directories(QuickLockStateTest PRIVATE ../../src)
target_link_libraries(QuickLockStateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(QuickLockStateTest)
add_executable(HoldShortcutControllerTest HoldShortcutControllerTest.cpp)
target_include_directories(HoldShortcutControllerTest PRIVATE ../../src)
target_link_libraries(HoldShortcutControllerTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HoldShortcutControllerTest)

add_executable(PhysicalShortcutsTest PhysicalShortcutsTest.cpp ../../src/personalization/PhysicalShortcuts.cpp)
target_include_directories(PhysicalShortcutsTest PRIVATE stubs ../../src)
target_link_libraries(PhysicalShortcutsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PhysicalShortcutsTest)

add_test(NAME QuickLockLifecycle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lock_lifecycle.py)

add_test(NAME ReaderBackOverride COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_back_override.py)
