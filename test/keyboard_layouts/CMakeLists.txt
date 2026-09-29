# See timezone/CMakeLists.txt: the generated string blob has to be marked GENERATED again in
# every subdirectory that names it, or a clean clone fails to configure.
set_source_files_properties(${I18N_GENERATED_STRINGS} PROPERTIES GENERATED TRUE)

add_executable(KeyboardLayoutSetTest
  KeyboardLayoutSetTest.cpp
  ${REPO_ROOT}/src/activities/util/KeyboardLayoutSet.cpp
  # The generated English string blob: I18n's constructor points at it. Data only.
  ${I18N_GENERATED_STRINGS}
)

# I18n.h (for Language and the singleton the test stubs), CrossPointSettings.h
# with the stdio-backed shims it pulls in (the same set HomeMenuTest uses), and
# the SDK keyboard header (for KeyboardLayoutId). The test defines the I18n
# members and the settings instance KeyboardLayoutSet reaches; nothing here
# draws a key.
target_include_directories(KeyboardLayoutSetTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/I18n
  ${I18N_GENERATED_DIR}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/test/epub_pipeline
  ${REPO_ROOT}/test/zip_entry_reader
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)

add_dependencies(KeyboardLayoutSetTest i18n_generated)

target_link_libraries(KeyboardLayoutSetTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KeyboardLayoutSetTest)
