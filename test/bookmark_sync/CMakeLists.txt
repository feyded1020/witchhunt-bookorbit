add_executable(BookmarkSyncTest
  BookmarkSyncTest.cpp
)

target_include_directories(BookmarkSyncTest PRIVATE
  ${REPO_ROOT}/test/shims       # HalStorage.h
  ${REPO_ROOT}/src              # BookmarkStore.h, Bookmark.h
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Logging
)

# Logging.h's few non-inline symbols (see css_gaps for the rationale).
target_sources(BookmarkSyncTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/LoggingStub.cpp
)

target_link_libraries(BookmarkSyncTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookmarkSyncTest)
