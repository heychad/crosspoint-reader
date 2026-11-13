add_executable(PersonalUpdateVersionTest
  PersonalUpdateVersionTest.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateVersion.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateTransport.cpp
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

if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
endif()
add_executable(PersonalImageTest PersonalImageTest.cpp ${REPO_ROOT}/src/network/PersonalImage.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp)
target_include_directories(PersonalImageTest PRIVATE stubs ${REPO_ROOT}/src/network)
target_compile_definitions(PersonalImageTest PRIVATE FREEINK_DEVICE_X4PRO=1)
target_link_libraries(PersonalImageTest PRIVATE crosspoint_test_common GTest::gtest_main)
if(NOT APPLE)
  target_link_libraries(PersonalImageTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(PersonalImageTest)

add_executable(OtaPipelineTest OtaPipelineTest.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp ${REPO_ROOT}/src/network/PersonalImage.cpp
  ${REPO_ROOT}/src/network/PersonalRelease.cpp ${REPO_ROOT}/src/network/PersonalUpdateVersion.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp)
target_include_directories(OtaPipelineTest PRIVATE stubs ${REPO_ROOT}/src/network ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include)
target_compile_definitions(OtaPipelineTest PRIVATE FREEINK_DEVICE_X4PRO=1 CROSSPOINT_VERSION="2.0.0-chad.3-test58-x4pro")
target_link_libraries(OtaPipelineTest PRIVATE crosspoint_test_common GTest::gtest_main)
if(NOT APPLE)
  target_link_libraries(OtaPipelineTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(OtaPipelineTest)

add_executable(PersonalTransportTest PersonalTransportTest.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateTransport.cpp
  ${REPO_ROOT}/src/network/PersonalUpdatePolicy.cpp
  ${REPO_ROOT}/src/network/PersonalUpdateVersion.cpp)
target_include_directories(PersonalTransportTest PRIVATE stubs ${REPO_ROOT}/src/network ${REPO_ROOT}/lib/Memory)
target_compile_definitions(PersonalTransportTest PRIVATE CROSSPOINT_EXPERIMENTAL_PERSONAL_UPDATES=1
  FREEINK_NET_WOLFSSL=1 FREEINK_DEVICE_X4PRO=1)
target_link_libraries(PersonalTransportTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PersonalTransportTest)
