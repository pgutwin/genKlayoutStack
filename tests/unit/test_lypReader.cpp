#include "gtest/gtest.h"
#include "gks/io/LypReader.hpp"
#include "gks/io/LypWriter.hpp"
#include "gks/core/LayerStack.hpp"

#include <filesystem>
#include <string>

static const std::string kFixturesDir = GKS_FIXTURES_DIR;

// ── Parse fixtures/default.lyp ──────────────────────────────────────────────

class DefaultLypTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto r = gks::readLyp(kFixturesDir + "/default.lyp");
        ASSERT_TRUE(r.has_value()) << r.error().message;
        stack = std::move(*r);
    }
    gks::LayerStack stack;
};

TEST_F(DefaultLypTest, LayerCount) {
    // default.lyp has 43 <properties> elements
    EXPECT_EQ(stack.layers.size(), 43u);
}

TEST_F(DefaultLypTest, AllLayersHaveNoPhysical) {
    for (const auto& layer : stack.layers) {
        EXPECT_FALSE(layer.physical.has_value())
            << "layer " << layer.layer_num << "/" << layer.datatype
            << " should have no physical props after lyp import";
    }
}

TEST_F(DefaultLypTest, FirstLayerSource) {
    // first entry: source="1/0@1"
    EXPECT_EQ(stack.layers[0].layer_num, 1);
    EXPECT_EQ(stack.layers[0].datatype,  0);
}

TEST_F(DefaultLypTest, FirstLayerFillColor) {
    // fill-color="#ff80a8"
    const auto& c = stack.layers[0].display.fill_color;
    EXPECT_EQ(c.r, 0xff);
    EXPECT_EQ(c.g, 0x80);
    EXPECT_EQ(c.b, 0xa8);
}

TEST_F(DefaultLypTest, FirstLayerDitherPattern) {
    // <dither-pattern>I9</dither-pattern>
    EXPECT_EQ(stack.layers[0].display.dither_pattern, 9);
}

TEST_F(DefaultLypTest, FirstLayerLineStyleEmpty) {
    // <line-style/>  →  -1
    EXPECT_EQ(stack.layers[0].display.line_style, -1);
}

TEST_F(DefaultLypTest, SecondLayerDitherPattern) {
    // source="1/1@1", <dither-pattern>I5</dither-pattern>
    EXPECT_EQ(stack.layers[1].layer_num, 1);
    EXPECT_EQ(stack.layers[1].datatype,  1);
    EXPECT_EQ(stack.layers[1].display.dither_pattern, 5);
}

TEST_F(DefaultLypTest, ThirdLayerFillColor) {
    // source="2/0@1", fill-color="#c080ff"
    const auto& c = stack.layers[2].display.fill_color;
    EXPECT_EQ(c.r, 0xc0);
    EXPECT_EQ(c.g, 0x80);
    EXPECT_EQ(c.b, 0xff);
}

TEST_F(DefaultLypTest, EmptyNameProducesEmptyString) {
    // All layers in default.lyp have <name/> — should produce empty string
    for (const auto& layer : stack.layers) {
        EXPECT_EQ(layer.name, "")
            << "layer " << layer.layer_num << "/" << layer.datatype
            << " should have empty name";
    }
}

TEST_F(DefaultLypTest, BoolFieldsParsedCorrectly) {
    // All layers in default.lyp have valid=true, visible=true, transparent=false
    for (const auto& layer : stack.layers) {
        EXPECT_TRUE(layer.display.valid);
        EXPECT_TRUE(layer.display.visible);
        EXPECT_FALSE(layer.display.transparent);
    }
}

TEST_F(DefaultLypTest, WidthEmptyProducesNullopt) {
    // All layers have <width/>  →  nullopt
    for (const auto& layer : stack.layers) {
        EXPECT_FALSE(layer.display.width.has_value());
    }
}

// ── Missing file returns error ───────────────────────────────────────────────

TEST(LypReaderError, MissingFileFails) {
    auto r = gks::readLyp("/nonexistent/path/to/file.lyp");
    EXPECT_FALSE(r.has_value());
}

// ── Write then read back round-trip ─────────────────────────────────────────

TEST(LypRoundtrip, WriteReadPreservesFields) {
    // Build a small LayerStack manually
    gks::LayerStack orig;
    orig.tech_name = "TestTech";
    orig.version   = "1.0";

    gks::LayerEntry e1;
    e1.layer_num = 3;
    e1.datatype  = 0;
    e1.name      = "poly";
    e1.purpose   = "drawing";
    e1.display.fill_color  = {0xff, 0x00, 0x00};
    e1.display.frame_color = {0xcc, 0x00, 0x00};
    e1.display.dither_pattern = 9;
    e1.display.line_style     = -1;
    e1.display.valid    = true;
    e1.display.visible  = true;
    e1.display.transparent = false;
    e1.display.fill_brightness  = 0;
    e1.display.frame_brightness = 0;
    e1.display.width   = std::nullopt;
    e1.display.marked  = false;
    e1.display.xfill   = false;
    e1.display.animation = 0;
    e1.display.expanded  = false;
    e1.physical = std::nullopt;
    orig.layers.push_back(e1);

    gks::LayerEntry e2;
    e2.layer_num = 7;
    e2.datatype  = 1;
    e2.name      = "";
    e2.display.fill_color  = {0x00, 0x55, 0xff};
    e2.display.frame_color = {0x00, 0x33, 0xcc};
    e2.display.dither_pattern = 5;
    e2.display.line_style     = 3;
    e2.display.visible    = false;
    e2.display.transparent = true;
    e2.display.width       = 2;
    e2.display.expanded    = true;
    e2.physical = std::nullopt;
    orig.layers.push_back(e2);

    std::string tmp = "/tmp/gks_lypunit_test.lyp";
    auto wr = gks::writeLyp(orig, tmp);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rr = gks::readLyp(tmp);
    ASSERT_TRUE(rr.has_value()) << rr.error().message;
    const auto& back = *rr;

    ASSERT_EQ(back.layers.size(), 2u);

    // Writer reverses layer order; e2 (last in orig) is first in .lyp
    // Layer 0 in readback = e2
    EXPECT_EQ(back.layers[0].layer_num, 7);
    EXPECT_EQ(back.layers[0].datatype,  1);
    EXPECT_EQ(back.layers[0].name,      "");
    EXPECT_EQ(back.layers[0].display.fill_color,  e2.display.fill_color);
    EXPECT_EQ(back.layers[0].display.frame_color, e2.display.frame_color);
    EXPECT_EQ(back.layers[0].display.dither_pattern, 5);
    EXPECT_EQ(back.layers[0].display.line_style,     3);
    EXPECT_FALSE(back.layers[0].display.visible);
    EXPECT_TRUE(back.layers[0].display.transparent);
    ASSERT_TRUE(back.layers[0].display.width.has_value());
    EXPECT_EQ(*back.layers[0].display.width, 2);
    EXPECT_TRUE(back.layers[0].display.expanded);
    EXPECT_FALSE(back.layers[0].physical.has_value());

    // Layer 1 in readback = e1
    EXPECT_EQ(back.layers[1].layer_num, 3);
    EXPECT_EQ(back.layers[1].datatype,  0);
    EXPECT_EQ(back.layers[1].name,      "poly");
    EXPECT_EQ(back.layers[1].display.fill_color,  e1.display.fill_color);
    EXPECT_EQ(back.layers[1].display.frame_color, e1.display.frame_color);
    EXPECT_EQ(back.layers[1].display.dither_pattern, 9);
    EXPECT_EQ(back.layers[1].display.line_style,  -1);
    EXPECT_TRUE(back.layers[1].display.valid);
    EXPECT_TRUE(back.layers[1].display.visible);
    EXPECT_FALSE(back.layers[1].display.transparent);
    EXPECT_FALSE(back.layers[1].display.width.has_value());

    std::filesystem::remove(tmp);
}
