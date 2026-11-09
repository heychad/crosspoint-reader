add_executable(BookReadingStatsAtomicTest
  BookReadingStatsAtomicTest.cpp
  StatsSafeReplaceTest.cpp
  ReadingStatsSessionTest.cpp
  GlobalReadingStatsTest.cpp
  DeviceStatsBackupTest.cpp
  BookStatsBackupTest.cpp
  ReadingStatsStoreTest.cpp
  BookTrackingTest.cpp
  ReaderStatsFinishTest.cpp
  StatsSaveNoticeTest.cpp
  ReadingStatsDateEditTest.cpp
  ReadingChartModelTest.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsUtils.cpp
  ${REPO_ROOT}/src/activities/reader/BookReadingStats.cpp
  ${REPO_ROOT}/src/activities/reader/StatsSafeReplace.cpp
  ${REPO_ROOT}/src/activities/reader/GlobalReadingStats.cpp
  ${REPO_ROOT}/src/activities/reader/DeviceStatsBackup.cpp
  ${REPO_ROOT}/src/activities/reader/BookStatsBackup.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingSessionStats.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsSession.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsDateEdit.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsStore.cpp
  ${REPO_ROOT}/src/activities/reader/BookTracking.cpp
  ${REPO_ROOT}/src/activities/reader/ReaderStats.cpp
  ${REPO_ROOT}/src/activities/reader/BookCompletion.cpp
)

target_include_directories(BookReadingStatsAtomicTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(BookReadingStatsAtomicTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookReadingStatsAtomicTest)
