#include "gtest/gtest.h"
#include "gks/io/ScriptReader.hpp"
#include "gks/core/LayerStack.hpp"

#include <fstream>
#include <string>
#include <filesystem>

static const std::string kFixturesDir = GKS_FIXTURES_DIR;

namespace {

// Helper: write content to a temp file
void writeTmpFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

// Helper: find a RawLayer by layer_num
const gks::RawLayer* findLayer(const std::vector<gks::RawLayer>& layers, int ln) {
    for (const auto& rl : layers) {
        if (rl.layer_num.has_value() && *rl.layer_num == ln) {
            return &rl;
        }
    }
    return nullptr;
}

} // anonymous namespace

// Test 1: Parse the ASAP7 fixture
TEST(ScriptReader, ParseAsap7Fixture) {
    auto r = gks::readScript(kFixturesDir + "/asap7_25d.rb");
    ASSERT_TRUE(r.has_value()) << r.error().message;

    // 9 physical layers (label is display-only, omitted from script)
    EXPECT_EQ(r->layers.size(), 9u);

    // substrate (layer_num=0, datatype=0)
    const auto* substrate = findLayer(r->layers, 0);
    ASSERT_NE(substrate, nullptr);
    ASSERT_TRUE(substrate->datatype.has_value());
    EXPECT_EQ(*substrate->datatype, 0);
    ASSERT_TRUE(substrate->z_start_nm.has_value());
    EXPECT_NEAR(*substrate->z_start_nm, -300.0, 1e-6);
    ASSERT_TRUE(substrate->thickness_nm.has_value());
    EXPECT_NEAR(*substrate->thickness_nm, 300.0, 1e-6);
    // fill_color: #888888
    ASSERT_TRUE(substrate->fill_color.has_value());
    EXPECT_EQ(substrate->fill_color->r, 0x88);
    EXPECT_EQ(substrate->fill_color->g, 0x88);
    EXPECT_EQ(substrate->fill_color->b, 0x88);

    // m0 (layer_num=6)
    const auto* m0 = findLayer(r->layers, 6);
    ASSERT_NE(m0, nullptr);
    ASSERT_TRUE(m0->z_start_nm.has_value());
    EXPECT_NEAR(*m0->z_start_nm, 150.0, 1e-6);
    ASSERT_TRUE(m0->thickness_nm.has_value());
    EXPECT_NEAR(*m0->thickness_nm, 36.0, 1e-6);
}

// Test 2: Unit suffix .nm
TEST(ScriptReader, UnitSuffixNm) {
    std::string path = "/tmp/gks_sr_nm.rb";
    writeTmpFile(path,
        "z(input(1, 0), zstart: 100.0.nm, height: 50.0.nm, fill: 0xff0000, frame: 0x0000ff, name: \"test\")\n");

    auto r = gks::readScript(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->layers.size(), 1u);

    ASSERT_TRUE(r->layers[0].z_start_nm.has_value());
    EXPECT_NEAR(*r->layers[0].z_start_nm, 100.0, 1e-6);
    ASSERT_TRUE(r->layers[0].thickness_nm.has_value());
    EXPECT_NEAR(*r->layers[0].thickness_nm, 50.0, 1e-6);

    std::filesystem::remove(path);
}

// Test 3: Unit suffix .um (multiply by 1000)
TEST(ScriptReader, UnitSuffixUm) {
    std::string path = "/tmp/gks_sr_um.rb";
    writeTmpFile(path,
        "z(input(1, 0), zstart: 0.1.um, height: 0.05.um, fill: 0xff0000, frame: 0x0000ff, name: \"test\")\n");

    auto r = gks::readScript(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->layers.size(), 1u);

    ASSERT_TRUE(r->layers[0].z_start_nm.has_value());
    EXPECT_NEAR(*r->layers[0].z_start_nm, 100.0, 1e-6);
    ASSERT_TRUE(r->layers[0].thickness_nm.has_value());
    EXPECT_NEAR(*r->layers[0].thickness_nm, 50.0, 1e-6);

    std::filesystem::remove(path);
}

// Test 4: zz() block parsing
TEST(ScriptReader, ZzBlockParsed) {
    std::string path = "/tmp/gks_sr_zz.rb";
    writeTmpFile(path,
        "zz(name: \"contacts\", fill: 0xff8800, frame: 0xcc6600) do\n"
        "  z(input(4, 0), zstart: 105.0.nm, height: 45.0.nm)\n"
        "  z(input(5, 0), zstart: 105.0.nm, height: 45.0.nm)\n"
        "end\n");

    auto r = gks::readScript(path);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->layers.size(), 2u);

    // First inner layer (4,0): has layer_expression
    ASSERT_TRUE(r->layers[0].layer_num.has_value());
    EXPECT_EQ(*r->layers[0].layer_num, 4);
    ASSERT_TRUE(r->layers[0].layer_expression.has_value());
    EXPECT_EQ(*r->layers[0].layer_expression, "input(4,0) + input(5,0)");

    // Second inner layer (5,0): no layer_expression
    ASSERT_TRUE(r->layers[1].layer_num.has_value());
    EXPECT_EQ(*r->layers[1].layer_num, 5);
    EXPECT_FALSE(r->layers[1].layer_expression.has_value());

    // Both layers get fill_color from outer zz() = 0xff8800
    ASSERT_TRUE(r->layers[0].fill_color.has_value());
    EXPECT_EQ(r->layers[0].fill_color->r, 0xff);
    EXPECT_EQ(r->layers[0].fill_color->g, 0x88);
    EXPECT_EQ(r->layers[0].fill_color->b, 0x00);

    ASSERT_TRUE(r->layers[1].fill_color.has_value());
    EXPECT_EQ(r->layers[1].fill_color->r, 0xff);
    EXPECT_EQ(r->layers[1].fill_color->g, 0x88);
    EXPECT_EQ(r->layers[1].fill_color->b, 0x00);

    std::filesystem::remove(path);
}
