add_executable(ChapterPageSpanTest
  ChapterPageSpanTest.cpp
)

target_include_directories(ChapterPageSpanTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ChapterPageSpanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ChapterPageSpanTest)
