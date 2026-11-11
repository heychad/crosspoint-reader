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
  CompletionEditTest.cpp
  CompletionJournalTest.cpp
  CompletionMovePlanTest.cpp
  DeferredCompletionIntentTest.cpp
  DeferredCompletionIntentIOTest.cpp
  MoveReferenceSafeWriteTest.cpp
  CompletionMovePlanIOTest.cpp
  CompletionMoveFileTest.cpp
  CrossInkBookmarkMoveTest.cpp
  CompletionJournalIOTest.cpp
  CompletionSnapshotValidationTest.cpp
  CompletionReplayCheckTest.cpp
  CompletionTransactionTest.cpp
  DeferredCompletionRecoveryTest.cpp
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
  ${REPO_ROOT}/src/activities/reader/CompletionEdit.cpp
  ${REPO_ROOT}/src/activities/reader/CompletionJournalIO.cpp
  ${REPO_ROOT}/test/CompletionArchiveInventoryStub.cpp
  ${REPO_ROOT}/src/activities/reader/CompletionLibraryDirty.cpp
  ${REPO_ROOT}/src/activities/reader/DeferredCompletionIntentIO.cpp
  ${REPO_ROOT}/src/activities/reader/DeferredCompletionMove.cpp
  ${REPO_ROOT}/src/activities/reader/DeferredCompletionRecovery.cpp
  ${REPO_ROOT}/src/activities/reader/DeferredCompletionMoveRetirement.cpp
  ${REPO_ROOT}/lib/Serialization/MoveReferenceSafeWrite.cpp
  ${REPO_ROOT}/src/activities/reader/CompletionMovePlanIO.cpp
  ${REPO_ROOT}/src/activities/reader/CompletionMoveFile.cpp
  ${REPO_ROOT}/src/activities/reader/CrossInkBookmarkMove.cpp
  ${REPO_ROOT}/src/activities/reader/CrossInkBookmarkEdit.cpp
  ${REPO_ROOT}/src/activities/reader/CompletionTransaction.cpp
  ${REPO_ROOT}/src/activities/reader/StatsSessionLease.cpp
  ${REPO_ROOT}/src/activities/reader/StatsAccessGate.cpp
)

target_include_directories(BookReadingStatsAtomicTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(BookReadingStatsAtomicTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookReadingStatsAtomicTest)

add_executable(DeviceStatsEnabledBackupTest
  DeviceStatsEnabledBackupTest.cpp
  ${REPO_ROOT}/src/activities/reader/DeviceStatsBackup.cpp
  ${REPO_ROOT}/src/activities/reader/GlobalReadingStats.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsUtils.cpp
  ${REPO_ROOT}/src/activities/reader/StatsSafeReplace.cpp
  ${REPO_ROOT}/src/activities/reader/StatsSessionLease.cpp
)
target_include_directories(DeviceStatsEnabledBackupTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/Serialization
)
target_compile_definitions(DeviceStatsEnabledBackupTest PRIVATE CROSSPOINT_EXPERIMENTAL_STATS_BACKUP=1)
target_link_libraries(DeviceStatsEnabledBackupTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DeviceStatsEnabledBackupTest)

# Keep the normal suite unchanged and exercise the optional recorder separately.
get_target_property(DAILY_STATS_SOURCES BookReadingStatsAtomicTest SOURCES)
add_executable(BookReadingStatsDailyTest
  ${DAILY_STATS_SOURCES}
  DailyReaderStatsTest.cpp
  ${REPO_ROOT}/src/activities/reader/DailyReadingStatsSession.cpp
  ${REPO_ROOT}/src/activities/reader/DailyReadingStatsStore.cpp
)
target_include_directories(BookReadingStatsDailyTest PRIVATE
  ../daily_reading_stats/stubs
  stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/Serialization
)
target_compile_definitions(BookReadingStatsDailyTest PRIVATE CROSSPOINT_EXPERIMENTAL_DAILY_STATS=1)
target_link_libraries(BookReadingStatsDailyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookReadingStatsDailyTest TEST_PREFIX DailyEnabled.)
