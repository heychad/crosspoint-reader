foreach(target ClippingStoreTest ClippingStoreS3Test)
  add_executable(${target} ClippingStoreTest.cpp ${REPO_ROOT}/src/ClippingStore.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  target_include_directories(${target} PRIVATE stubs ../library_builder/stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Utf8)
  gtest_discover_tests(${target} TEST_PREFIX "${target}.")
endforeach()
target_compile_definitions(ClippingStoreS3Test PRIVATE CONFIG_IDF_TARGET_ESP32S3=1)

add_executable(ClippingsExportTest ClippingsExportTest.cpp
  ${REPO_ROOT}/src/clippings/ClippingsManager.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_link_libraries(ClippingsExportTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(ClippingsExportTest PRIVATE ../library_builder/stubs ../daily_reading_stats/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Utf8)
gtest_discover_tests(ClippingsExportTest)
