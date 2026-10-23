#include <audio/audio_backend.hpp>
#include <doctest/doctest.h>
#include <nova/platform/file.hpp>
#include <platform/asset_io.hpp>
#include <render/renderer_backend.hpp>
#include <text/text_backend.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace nova;
namespace fs = std::filesystem;

namespace {

// NOVA_TEST_PACK is "<build>/test-pack/game.pak", built from the Brick Breaker assets with data/test.key.
const fs::path kPack = NOVA_TEST_PACK;
const fs::path kLooseRoot = NOVA_TEST_ASSET_ROOT;

std::vector<std::string>& failures() {
    static std::vector<std::string> list;
    return list;
}

struct PackFixture {
    PackFixture() {
        failures().clear();
        setIntegrityFailureHandlerForTests([](const char* reason) { failures().emplace_back(reason); });
    }
    ~PackFixture() {
        unmountPack();
        setIntegrityFailureHandlerForTests(nullptr);
        setAssetRoot(NOVA_TEST_ASSET_ROOT);
        if (!tmp.empty()) fs::remove_all(tmp);
    }

    // A fresh folder for this test; asset root paths must end with a separator.
    fs::path tempDir() {
        if (tmp.empty()) {
            tmp = fs::temp_directory_path() / ("nova_pack_test_" + std::to_string(SDL_GetTicksNS()));
            fs::create_directories(tmp);
        }
        return tmp;
    }

    fs::path tmp;
};

std::string rootOf(const fs::path& dir) { return (dir / "").string(); }

// The asset root holds only game.pak, so a successful load proves the pack was used.
MountResult mountTestPack() {
    setAssetRoot(rootOf(kPack.parent_path()));
    return mountPack();
}

std::vector<char> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void writeBytes(const fs::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), std::streamsize(bytes.size()));
}

std::vector<char> readAll(SDL_IOStream* io) {
    REQUIRE(io);
    size_t size = 0;
    void* data = SDL_LoadFile_IO(io, &size, true);
    REQUIRE(data);
    std::vector<char> bytes(static_cast<char*>(data), static_cast<char*>(data) + size);
    SDL_free(data);
    return bytes;
}

std::vector<std::string> looseAssets() {
    std::vector<std::string> paths;
    for (const auto& item : fs::recursive_directory_iterator(kLooseRoot)) {
        fs::path rel = fs::relative(item.path(), kLooseRoot);
        if (item.is_regular_file() && rel.filename().string()[0] != '.') paths.push_back(rel.generic_string());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

int runTool(const std::string& args, const char* envKey = nullptr) {
    std::string cmd;
    if (envKey) cmd += std::string("NOVA_PACK_KEY=") + envKey + " ";
    cmd += "\"" NOVA_PACK_TOOL "\" " + args;
    return std::system(cmd.c_str());
}

std::string quoted(const fs::path& p) { return "\"" + p.string() + "\""; }

// Copies game.pak into a temp folder with one byte flipped (or the tail cut) and mounts it.
MountResult mountDamaged(PackFixture& fx, size_t flipAt, size_t truncateBy = 0) {
    std::vector<char> bytes = readBytes(kPack);
    if (truncateBy) bytes.resize(bytes.size() - truncateBy);
    else bytes[flipAt] = char(bytes[flipAt] ^ 0x01);
    writeBytes(fx.tempDir() / "game.pak", bytes);
    setAssetRoot(rootOf(fx.tempDir()));
    return mountPack();
}

} // namespace

TEST_CASE("Pack mounts and every asset is byte-identical to the loose file") {
    PackFixture fx;
    REQUIRE(mountTestPack() == MountResult::Mounted);
    CHECK(packMounted());
    auto paths = looseAssets();
    REQUIRE(paths.size() >= 8);
    for (const std::string& rel : paths) {
        CAPTURE(rel);
        CHECK(readAll(openAsset(rel)) == readBytes(kLooseRoot / rel));
    }
    CHECK(failures().empty());
}

TEST_CASE("Pack is used by readTextFile, textures, sounds and fonts") {
    PackFixture fx;
    REQUIRE(mountTestPack() == MountResult::Mounted);

    auto atlas = readTextFile("sprites.atlas");
    REQUIRE(atlas.ok());
    CHECK(atlas.value().find("paddle") != std::string::npos);
    CHECK(readFile("sfx/hit.wav").ok());

    SDL_Surface* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* sw = SDL_CreateSoftwareRenderer(surface);
    REQUIRE(sw);
    {
        auto text = TextBackend::create(sw);
        REQUIRE(text.ok());
        CHECK(text.value()->loadFont("fonts/Fredoka.ttf@32").has_value());
    }
    SDL_DestroyRenderer(sw);
    SDL_DestroySurface(surface);

    REQUIRE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    SDL_Window* window = SDL_CreateWindow("pack", 360, 640, 0);
    REQUIRE(window);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "NOVA_RENDER_DRIVER", "software", true);
    {
        auto renderer = RendererBackend::create(window, {720, 1280}, Color::Black);
        REQUIRE(renderer.ok());
        TextureHandle tex = renderer.value()->loadTexture("sprites.png");
        REQUIRE(tex.valid());
        CHECK(renderer.value()->textureSize(tex) == Vec2{256, 256});
    }
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "NOVA_RENDER_DRIVER");
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO));
    {
        auto audio = AudioBackend::create();
        REQUIRE(audio);
        CHECK(audio->loadSound("sfx/hit.wav").has_value());
        CHECK(audio->loadMusic("music/loop.wav").has_value());
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    CHECK(failures().empty());
}

TEST_CASE("Pack compresses wav and ttf, and only stores an entry compressed when it shrinks") {
    PackFixture fx;
    REQUIRE(mountTestPack() == MountResult::Mounted);
    for (const char* path : {"music/loop.wav", "fonts/Fredoka.ttf"}) {
        const pack::Entry* e = packEntryForTests(path);
        REQUIRE(e);
        CHECK(e->compression == pack::kZstd);
    }
    for (const std::string& rel : looseAssets()) {
        const pack::Entry* e = packEntryForTests(rel);
        REQUIRE(e);
        CHECK(e->originalSize == fs::file_size(kLooseRoot / rel));
        if (e->compression == pack::kRaw) CHECK(e->storedSize == e->originalSize);
        else CHECK(uint64_t(e->storedSize) * 100 <= uint64_t(e->originalSize) * 95);
    }
}

TEST_CASE("Pack unknown path is a normal missing asset") {
    PackFixture fx;
    REQUIRE(mountTestPack() == MountResult::Mounted);
    CHECK(openAsset("nope.png") == nullptr);
    CHECK(!readFile("sprites.atlas.bak").ok());
    CHECK(failures().empty());
}

TEST_CASE("Pack mount fails on wrong key") {
    PackFixture fx;
    fs::path other = fx.tempDir() / "game.pak";
    REQUIRE(runTool("pack " + quoted(kLooseRoot) + " " + quoted(other),
                    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff") == 0);
    setAssetRoot(rootOf(fx.tempDir()));
    CHECK(mountPack() == MountResult::Corrupt);
    CHECK(!packMounted());
    REQUIRE(failures().size() == 1);
    CHECK(failures()[0].find("index") != std::string::npos);
}

TEST_CASE("Pack detects a flipped byte in header, index and entry data") {
    PackFixture fx;
    SUBCASE("header") {
        CHECK(mountDamaged(fx, 20) == MountResult::Corrupt);
        CHECK(failures().size() == 1);
    }
    SUBCASE("index") {
        CHECK(mountDamaged(fx, size_t(fs::file_size(kPack)) - 1) == MountResult::Corrupt);
        CHECK(failures().size() == 1);
    }
    SUBCASE("entry data") {
        REQUIRE(mountTestPack() == MountResult::Mounted);
        std::string first;
        for (const std::string& rel : looseAssets()) {
            if (packEntryForTests(rel)->dataOff == pack::kHeaderSize) first = rel;
        }
        REQUIRE(!first.empty());
        unmountPack();
        REQUIRE(mountDamaged(fx, 100) == MountResult::Mounted);
        CHECK(failures().empty());
        CHECK(openAsset(first) == nullptr);
        REQUIRE(failures().size() == 1);
        CHECK(failures()[0].find("entry") != std::string::npos);
    }
}

TEST_CASE("Pack detects truncation") {
    PackFixture fx;
    CHECK(mountDamaged(fx, 0, 100) == MountResult::Corrupt);
    CHECK(failures().size() == 1);
    CHECK(!packMounted());
}

TEST_CASE("Pack reads entries whose paths contain spaces") {
    PackFixture fx;
    fs::path assets = fx.tempDir() / "assets";
    fs::create_directories(assets / "ui");
    writeBytes(assets / "ui" / "my button.txt", {'o', 'k'});
    REQUIRE(runTool("pack " + quoted(assets) + " " + quoted(fx.tempDir() / "out" / "game.pak") +
                    " --key-file " + quoted(NOVA_TEST_KEY_FILE)) == 0);
    setAssetRoot(rootOf(fx.tempDir() / "out"));
    REQUIRE(mountPack() == MountResult::Mounted);
    auto text = readTextFile("ui/my button.txt");
    REQUIRE(text.ok());
    CHECK(text.value() == "ok");
}

TEST_CASE("No pack present means loose files") {
    PackFixture fx;
    CHECK(mountPack("absent.pak") == MountResult::NotPresent);
    CHECK(!packMounted());
    CHECK(readTextFile("sprites.atlas").ok());
    CHECK(readAll(openAsset("sprites.atlas")) == readBytes(kLooseRoot / "sprites.atlas"));
    CHECK(failures().empty());
}
