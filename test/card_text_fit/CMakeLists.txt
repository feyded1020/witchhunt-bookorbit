add_executable(CardTextFitTest
  CardTextFitTest.cpp
)

target_include_directories(CardTextFitTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(CardTextFitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CardTextFitTest)
