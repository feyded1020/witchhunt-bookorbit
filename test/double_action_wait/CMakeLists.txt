add_executable(DoubleActionWaitTest
  DoubleActionWaitTest.cpp
)

target_include_directories(DoubleActionWaitTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(DoubleActionWaitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DoubleActionWaitTest)
