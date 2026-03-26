#include "gtest/gtest.h"
#include "gks/io/TomlReader.hpp"
#include "gks/io/ScriptWriter.hpp"
#include "gks/io/ScriptReader.hpp"
#include "gks/core/LayerStack.hpp"

#include <filesystem>
#include <fstream>
#include <string>

static const std::string kFixturesDir = GKS_FIXTURES_DIR;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

gks::LayerStack makeNegZStack() {
    gks::LayerStack s;
    s.tech_name = "TEST";
    s.version   = "0.1";

    gks::LayerEntry e;
    e.layer_num = 0;
    e.datatype  = 0;
    e.name      = "substrate";
    e.display.fill_color  = {0x88, 0x88, 0x88};
    e.display.frame_color = {0x44, 0x44, 0x44};
    gks::PhysicalProps p;
    p.z_start_nm   = -300.0;
    p.thickness_nm = 300.0;
    p.material     = "silicon";
    e.physical = p;
    s.layers.push_back(e);
    return s;
}

} // anonymous namespace

// Test 1: Negative z value is emitted correctly
TEST(NegativeZ, NegativeZEmitted) {
    auto stack = makeNegZStack();
    std::string path = "/tmp/gks_negz_emit.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);
    EXPECT_NE(content.find("zstart: -300.0.nm"), std::string::npos);

    std::filesystem::remove(path);
}

// Test 2: Negative z value is parsed back correctly
TEST(NegativeZ, NegativeZParsed) {
    auto stack = makeNegZStack();
    std::string path = "/tmp/gks_negz_parse.rb";
    auto wr = gks::writeScript(stack, path);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rr = gks::readScript(path);
    ASSERT_TRUE(rr.has_value()) << rr.error().message;
    ASSERT_EQ(rr->layers.size(), 1u);

    ASSERT_TRUE(rr->layers[0].z_start_nm.has_value());
    EXPECT_NEAR(*rr->layers[0].z_start_nm, -300.0, 1e-6);

    std::filesystem::remove(path);
}

// Test 3: Full round-trip using asap7_stack.toml — substrate and bspdn have negative z
TEST(NegativeZ, NegativeZRoundtrip) {
    auto r = gks::readToml(kFixturesDir + "/asap7_stack.toml");
    ASSERT_TRUE(r.has_value()) << r.error().message;

    auto br = gks::buildStack(r->tech_name, r->version, r->layers, r->defaults);

    std::string path = "/tmp/gks_negz_roundtrip.rb";
    auto wr = gks::writeScript(br.stack, path);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rr = gks::readScript(path);
    ASSERT_TRUE(rr.has_value()) << rr.error().message;

    // Find substrate (0,0) and bspdn (1,0)
    const gks::RawLayer* substrate = nullptr;
    const gks::RawLayer* bspdn     = nullptr;
    for (const auto& rl : rr->layers) {
        if (rl.layer_num.has_value()) {
            if (*rl.layer_num == 0) substrate = &rl;
            if (*rl.layer_num == 1) bspdn     = &rl;
        }
    }

    ASSERT_NE(substrate, nullptr);
    ASSERT_TRUE(substrate->z_start_nm.has_value());
    EXPECT_NEAR(*substrate->z_start_nm, -300.0, 1e-6);

    ASSERT_NE(bspdn, nullptr);
    ASSERT_TRUE(bspdn->z_start_nm.has_value());
    EXPECT_NEAR(*bspdn->z_start_nm, -150.0, 1e-6);

    std::filesystem::remove(path);
}
