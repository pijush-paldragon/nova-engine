#define DOCTEST_CONFIG_IMPLEMENT
#include "support/run_tests.hpp"

int main(int argc, char** argv) { return nova::testing::runTests(argc, argv, NOVA_TEST_ASSET_ROOT); }
