#include <doctest/doctest.h>
#include <nova/save/save_store.hpp>

#include <SDL3/SDL.h>

#include <string>

using namespace nova;

namespace {

std::string freshDir() {
    char* pref = SDL_GetPrefPath("nova-tests", "save");
    std::string dir = pref;
    SDL_free(pref);
    SDL_RemovePath((dir + "save.dat").c_str());
    SDL_RemovePath((dir + "save.dat.tmp").c_str());
    return dir;
}

bool fileExists(const std::string& path) { return SDL_GetPathInfo(path.c_str(), nullptr); }

void writeFile(const std::string& path, const char* text) {
    SDL_SaveFile(path.c_str(), text, SDL_strlen(text));
}

} // namespace

TEST_CASE("SaveStore round-trips all types including tricky strings") {
    std::string dir = freshDir();
    {
        SaveStore s{dir};
        s.setInt("best_score", 12345);
        s.setFloat("vol", 0.25);
        s.setBool("muted", true);
        s.setString("name", "a b\nc\\d\r");
        CHECK(s.dirty());
        REQUIRE(s.flush().ok());
        CHECK(!s.dirty());
    }
    SaveStore r{dir};
    REQUIRE(r.load());
    CHECK(r.getInt("best_score") == 12345);
    CHECK(r.getFloat("vol") == 0.25);
    CHECK(r.getBool("muted"));
    CHECK(r.getString("name") == "a b\nc\\d\r");
    CHECK(r.getInt("vol", -1) == -1); // type mismatch -> fallback
    CHECK(r.getInt("missing", 9) == 9);
    CHECK(!fileExists(dir + "save.dat.tmp")); // atomic write leaves no temp file
}

TEST_CASE("SaveStore recovers from corrupt file") {
    std::string dir = freshDir();
    writeFile(dir + "save.dat", "garbage\x01\x02");
    SaveStore s{dir};
    CHECK(!s.load());
    CHECK(s.getInt("x", 7) == 7);
    s.setInt("x", 1);
    CHECK(s.flush().ok());
    SaveStore r{dir};
    CHECK(r.load());
    CHECK(r.getInt("x") == 1);
}

TEST_CASE("SaveStore skips malformed lines but keeps valid ones") {
    std::string dir = freshDir();
    writeFile(dir + "save.dat", "NOVA_SAVE 1\ni good 5\ni bad notanumber\nzz what 1\nb flag 1\n");
    SaveStore s{dir};
    CHECK(s.load());
    CHECK(s.getInt("good") == 5);
    CHECK(s.getInt("bad", -1) == -1);
    CHECK(s.getBool("flag"));
}

TEST_CASE("SaveStore flush is a no-op when clean") {
    std::string dir = freshDir();
    SaveStore s{dir};
    CHECK(s.flush().ok());
    CHECK(!fileExists(dir + "save.dat"));
}

TEST_CASE("SaveStore rejects invalid keys") {
    std::string dir = freshDir();
    SaveStore s{dir};
    s.setInt("has space", 1);
    s.setInt("", 1);
    CHECK(!s.dirty());
    CHECK(s.getInt("has space", 3) == 3);
}
