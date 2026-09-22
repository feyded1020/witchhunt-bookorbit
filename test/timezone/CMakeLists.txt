add_executable(TimezoneTest
  TimezoneTest.cpp
)

target_include_directories(TimezoneTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(TimezoneTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TimezoneTest)
