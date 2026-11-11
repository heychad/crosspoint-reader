add_executable(DailyReadingStatsStoreTest
  StoreTest.cpp
  SessionTest.cpp
  ${REPO_ROOT}/src/activities/reader/DailyReadingStatsSession.cpp
  ${REPO_ROOT}/src/activities/reader/DailyReadingStatsStore.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsUtils.cpp
)
target_include_directories(DailyReadingStatsStoreTest PRIVATE
  ../book_reading_stats_atomic/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(DailyReadingStatsStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DailyReadingStatsStoreTest)
