# ---------------------------------------------------------------------------
# DISABLED on MinGW: the local linker crashes, and the fault is not in this code.
#
# GNU ld 2.47.20260726 (the UCRT64 snapshot on this machine) dies with no diagnostic at all --
# "collect2.exe: error: ld returned 5 exit status", exit 5 being a truncated 0xC0000005 access
# violation -- when an executable links a translation unit that calls
# InflateReader::setReadCallback() together with inflate_reader_host/uzlib_host. It produces a
# truncated .exe and emits nothing even under -Wl,--verbose, so there is no symbol to chase.
#
# Proven not to be this suite's code: a main() containing nothing but InflateReader::init(),
# setReadCallback() and deinit() -- no ContentProtection, no miniz, none of our classes --
# crashes the same way, while ZipEntryReaderTest links fine against the same archives. All
# symbols the objects need are present (checked with nm against every archive in the link).
#
# Left registered but skipped so the test builds and runs anywhere the toolchain is sound
# (CI, Linux, an older binutils). To try it here after a toolchain update:
#   cmake -S test -B build/test -DCROSSPOINT_FORCE_CONTENT_INFLATE_TEST=ON
# ---------------------------------------------------------------------------
if(MINGW AND NOT CROSSPOINT_FORCE_CONTENT_INFLATE_TEST)
  message(STATUS "content_inflate: skipped (GNU ld 2.47 crash, see this file's comment)")
  return()
endif()

# Both decompressors behind the ContentProtection Inflate seam, built against each other.
#
# MinizInflate needs the vendored miniz, which the firmware build drops (CONTENT_NO_MINIZ) —
# here it is compiled deliberately, because comparing our uzlib provider against the library's
# own default is the whole point of the suite.
set(CONTENT_DIR ${REPO_ROOT}/freeink-sdk/libs/book/ContentProtection)

add_executable(ContentInflateTest
  ContentInflateTest.cpp
  ${REPO_ROOT}/lib/Epub/UzlibContentInflate.cpp
  ${CONTENT_DIR}/src/MinizInflate.cpp
  ${CONTENT_DIR}/src/vendor/miniz_impl.c
)

target_include_directories(ContentInflateTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/InflateReader
  ${CONTENT_DIR}/include
  ${CONTENT_DIR}/third_party/miniz
)

target_link_libraries(ContentInflateTest PRIVATE
  inflate_reader_host
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ContentInflateTest)
