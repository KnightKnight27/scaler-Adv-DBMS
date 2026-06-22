// Single translation unit that defines the Catch2 entry point.  All other
// test_*.cpp files just #include the Catch2 header and register TEST_CASEs.
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
