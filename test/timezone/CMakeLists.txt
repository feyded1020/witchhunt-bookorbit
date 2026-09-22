# The string blob does not exist until the generator runs, and CMake rejects a source that is
# absent at CONFIGURE time unless it is marked GENERATED. The custom command in the parent
# CMakeLists declares it as an OUTPUT, but the GENERATED property is DIRECTORY-scoped (CMP0118),
# so that declaration does not reach this subdirectory -- it has to be marked again here.
#
# Without this, a tree that has already built configures fine and a clean clone does not, which
# is exactly the split between this machine and CI.
set_source_files_properties(${I18N_GENERATED_STRINGS} PROPERTIES GENERATED TRUE)

add_executable(TimezoneTest
  TimezoneTest.cpp
  ${REPO_ROOT}/src/util/Timezones.cpp
  # The generated English string blob, for the I18n stub at the top of the test. Data only --
  # the real I18n.cpp is not linkable here, it reaches into the flash language partition.
  # Via the variable, which is declared as a custom-command OUTPUT: naming the path directly
  # configures only in a tree that has already built once.
  ${I18N_GENERATED_STRINGS}
)

# The consistency group builds the timezone SettingInfo row, which reaches CrossPointSettings.h
# and I18n.h -- the same stub/shim set SettingsGroupingTest uses.
add_dependencies(TimezoneTest i18n_generated)

target_include_directories(TimezoneTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/I18n
  ${I18N_GENERATED_DIR}
  ${REPO_ROOT}/lib
  # I18n.h stub, and the stdio-backed HalStorage CrossPointSettings.h pulls in. These must
  # precede lib/hal: the real HalStorage.h there wants SdFat, which the host toolchain has no
  # business building.
  ${REPO_ROOT}/test/epub_pipeline
  ${REPO_ROOT}/test/zip_entry_reader
  ${REPO_ROOT}/test/shims
  # HalClock.h, whose applyTimezone() the test defines a recording stub for.
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(TimezoneTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TimezoneTest)
