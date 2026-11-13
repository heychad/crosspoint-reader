add_executable(PersonalUpdateVersionTest
  PersonalUpdateVersionTest.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateVersion.cpp
)
target_include_directories(PersonalUpdateVersionTest PRIVATE ${REPO_ROOT}/src/network)
target_link_libraries(PersonalUpdateVersionTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PersonalUpdateVersionTest)

add_executable(PersonalReleaseTest PersonalReleaseTest.cpp
  ${REPO_ROOT}/src/network/PersonalRelease.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateVersion.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
target_include_directories(PersonalReleaseTest PRIVATE ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_link_libraries(PersonalReleaseTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PersonalReleaseTest)
