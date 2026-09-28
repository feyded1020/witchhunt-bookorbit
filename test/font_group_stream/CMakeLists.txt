enable_language(C)

# The real FontDecompressor::GroupStream over tinflate.c: the ring contract is a property of the
# DEFLATE decoder plus the generated font tables, and GroupStream is the code that has to honour
# it on the device. Logging stays compiled out (no ENABLE_SERIAL_LOG), so no log stub is needed.
add_library(uzlib_font_ring STATIC ${REPO_ROOT}/lib/uzlib/src/tinflate.c)
target_include_directories(uzlib_font_ring PUBLIC ${REPO_ROOT}/lib/uzlib/src)

add_executable(FontGroupStreamTest
  FontGroupStreamTest.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/test/uzlib_lzcopy/UzlibChksumStubs.cpp
)
target_include_directories(FontGroupStreamTest PRIVATE
  # Our directory first: its Arduino.h adds micros() to the shared shim.
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/EpdFont/builtinFonts
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(FontGroupStreamTest PRIVATE
  crosspoint_test_common
  uzlib_font_ring
  GTest::gtest_main
)
gtest_discover_tests(FontGroupStreamTest)
