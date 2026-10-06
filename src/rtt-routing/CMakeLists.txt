# src/rtt-routing/CMakeLists.txt

# Build the module
build_lib(
  LIBNAME rtt-routing
  SOURCE_FILES
    model/ipv4-rtt-routing.cc
    model/ipv4-rtt-tag.cc
    helper/ipv4-rtt-routing-helper.cc
  HEADER_FILES
    model/ipv4-rtt-routing.h
    model/ipv4-rtt-tag.h
    helper/ipv4-rtt-routing-helper.h
  LIBRARIES_TO_LINK ${libinternet}
)

# Build the test library
build_lib(
  LIBNAME rtt-routing-test
  SOURCE_FILES
    test/ipv4-rtt-routing-test-suite.cc
  LIBRARIES_TO_LINK
    rtt-routing
)

