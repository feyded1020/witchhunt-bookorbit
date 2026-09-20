add_executable(ReleaseNotesTest
  ReleaseNotesTest.cpp
)

target_include_directories(ReleaseNotesTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReleaseNotesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReleaseNotesTest)
