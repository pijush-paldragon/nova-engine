// main() for game test targets created by nova_add_game_tests(): assets come from the game's
// own assets/ folder.
#define DOCTEST_CONFIG_IMPLEMENT
#include "run_tests.hpp"

int main(int argc, char** argv) { return nova::testing::runTests(argc, argv, NOVA_GAME_TEST_ASSET_ROOT); }
