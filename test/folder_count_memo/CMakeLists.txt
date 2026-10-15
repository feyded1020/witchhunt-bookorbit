add_executable(FolderCountMemoTest
  FolderCountMemoTest.cpp
  ${REPO_ROOT}/src/activities/home/FolderCountMemo.cpp
)

target_include_directories(FolderCountMemoTest PRIVATE
  ${REPO_ROOT}/src/activities/home
)

target_link_libraries(FolderCountMemoTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(FolderCountMemoTest)
