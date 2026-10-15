add_executable(BookDetailsCacheTest
  BookDetailsCacheTest.cpp
  ${REPO_ROOT}/src/activities/home/BookDetailsCache.cpp
)

target_include_directories(BookDetailsCacheTest PRIVATE
  ${REPO_ROOT}/src/activities/home
  ${REPO_ROOT}/lib/Serialization
  # stdio-backed HalStorage.h; shadows the test/shims stub, which cannot open files.
  ${REPO_ROOT}/test/zip_entry_reader
  ${REPO_ROOT}/test/shims
)

target_link_libraries(BookDetailsCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookDetailsCacheTest)
