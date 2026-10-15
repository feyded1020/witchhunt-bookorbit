add_executable(ListRowCacheTest
  ListRowCacheTest.cpp
)

target_include_directories(ListRowCacheTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ListRowCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListRowCacheTest)
