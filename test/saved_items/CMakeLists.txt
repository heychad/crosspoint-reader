add_executable(SavedItemsTest
  SavedItemsTest.cpp
  ${REPO_ROOT}/src/util/SavedItems.cpp
  ${REPO_ROOT}/src/util/CrossInkBookmarks.cpp
  ${REPO_ROOT}/src/util/BookmarkFileClear.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp
  ${REPO_ROOT}/src/ClippingStore.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/CatalogList/src/JsonListParser.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)
target_include_directories(SavedItemsTest PRIVATE
  ../clipping_store/stubs ../library_builder/stubs
  ${REPO_ROOT}/src ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/freeink-sdk/libs/network/CatalogList/include
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)
target_link_libraries(SavedItemsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SavedItemsTest)
add_test(NAME SavedItemsNavigation COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_navigation.py)
add_test(NAME SavedItemsCatalog COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_catalog.py)
add_test(NAME SavedItemsHomeInput COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_home.py)
add_test(NAME DeferredCompletionHomeInput COMMAND ${CMAKE_COMMAND} -E env TEST_DEFERRED_AUTOMATION=1 ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_home.py)
