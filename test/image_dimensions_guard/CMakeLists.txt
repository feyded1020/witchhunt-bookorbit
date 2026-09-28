add_executable(ImageDimensionsGuardTest
  ImageDimensionsGuardTest.cpp
)

target_include_directories(ImageDimensionsGuardTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(ImageDimensionsGuardTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ImageDimensionsGuardTest)
