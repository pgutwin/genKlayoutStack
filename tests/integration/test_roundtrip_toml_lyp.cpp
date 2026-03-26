#include "gtest/gtest.h"
#include "gks/io/TomlReader.hpp"
#include "gks/io/TomlWriter.hpp"
#include "gks/core/LayerStack.hpp"

#include <filesystem>
#include <string>

static const std::string kFixturesDir = GKS_FIXTURES_DIR;

// ── Phase 3: TOML → LayerStack → TOML round-trip (field-by-field) ──────────

class TomlRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Read original TOML
        auto r1 = gks::readToml(kFixturesDir + "/asap7_stack.toml");
        ASSERT_TRUE(r1.has_value()) << r1.error().message;
        orig = std::move(*r1);

        // Build LayerStack
        auto br = gks::buildStack(orig.tech_name, orig.version, orig.layers, orig.defaults);
        stack = std::move(br.stack);

        // Write back to temp file
        tmp_path = "/tmp/gks_roundtrip_test.toml";
        auto w = gks::writeToml(stack, tmp_path);
        ASSERT_TRUE(w.has_value()) << w.error().message;

        // Read the written file back
        auto r2 = gks::readToml(tmp_path);
        ASSERT_TRUE(r2.has_value()) << r2.error().message;
        written = std::move(*r2);

        // Build LayerStack from written TOML
        auto br2 = gks::buildStack(written.tech_name, written.version,
                                    written.layers, written.defaults);
        stack2 = std::move(br2.stack);
    }

    void TearDown() override {
        std::filesystem::remove(tmp_path);
    }

    gks::TomlReadResult orig;
    gks::LayerStack     stack;
    std::string         tmp_path;
    gks::TomlReadResult written;
    gks::LayerStack     stack2;
};

TEST_F(TomlRoundtripTest, StackMetadataPreserved) {
    EXPECT_EQ(stack2.tech_name, stack.tech_name);
    EXPECT_EQ(stack2.version,   stack.version);
}

TEST_F(TomlRoundtripTest, LayerCountPreserved) {
    EXPECT_EQ(stack2.layers.size(), stack.layers.size());
}

TEST_F(TomlRoundtripTest, LayerKeysPreserved) {
    for (size_t i = 0; i < stack.layers.size(); ++i) {
        SCOPED_TRACE("layer index " + std::to_string(i));
        EXPECT_EQ(stack2.layers[i].layer_num, stack.layers[i].layer_num);
        EXPECT_EQ(stack2.layers[i].datatype,  stack.layers[i].datatype);
        EXPECT_EQ(stack2.layers[i].name,      stack.layers[i].name);
        EXPECT_EQ(stack2.layers[i].purpose,   stack.layers[i].purpose);
    }
}

TEST_F(TomlRoundtripTest, DisplayPropsPreserved) {
    for (size_t i = 0; i < stack.layers.size(); ++i) {
        SCOPED_TRACE("layer index " + std::to_string(i));
        const auto& d1 = stack.layers[i].display;
        const auto& d2 = stack2.layers[i].display;
        EXPECT_EQ(d2.fill_color,     d1.fill_color);
        EXPECT_EQ(d2.frame_color,    d1.frame_color);
        EXPECT_EQ(d2.fill_alpha,     d1.fill_alpha);
        EXPECT_EQ(d2.dither_pattern, d1.dither_pattern);
        EXPECT_EQ(d2.line_style,     d1.line_style);
        EXPECT_EQ(d2.visible,        d1.visible);
        EXPECT_EQ(d2.valid,          d1.valid);
        EXPECT_EQ(d2.transparent,    d1.transparent);
    }
}

TEST_F(TomlRoundtripTest, PhysicalPropsPreserved) {
    for (size_t i = 0; i < stack.layers.size(); ++i) {
        SCOPED_TRACE("layer '" + stack.layers[i].name + "' (index " + std::to_string(i) + ")");
        const bool had_phys = stack.layers[i].physical.has_value();
        EXPECT_EQ(stack2.layers[i].physical.has_value(), had_phys);

        if (had_phys && stack2.layers[i].physical.has_value()) {
            const auto& p1 = *stack.layers[i].physical;
            const auto& p2 = *stack2.layers[i].physical;
            EXPECT_NEAR(p2.z_start_nm,   p1.z_start_nm,   1e-6);
            EXPECT_NEAR(p2.thickness_nm, p1.thickness_nm,  1e-6);
            EXPECT_EQ(p2.material,       p1.material);
            EXPECT_EQ(p2.layer_expression, p1.layer_expression);
        }
    }
}

TEST_F(TomlRoundtripTest, NegativeZPreserved) {
    // substrate (index 0) should have z_start = -300
    const auto& layer = stack2.layers[0];
    ASSERT_TRUE(layer.physical.has_value());
    EXPECT_NEAR(layer.physical->z_start_nm, -300.0, 1e-6);
}

TEST_F(TomlRoundtripTest, ParallelGroupZPreserved) {
    // epi_contact (index 4) and gate_contact (index 5) share z_start = 105
    const auto& epi  = stack2.layers[4];
    const auto& gate = stack2.layers[5];
    ASSERT_TRUE(epi.physical.has_value());
    ASSERT_TRUE(gate.physical.has_value());
    EXPECT_NEAR(epi.physical->z_start_nm,  105.0, 1e-6);
    EXPECT_NEAR(gate.physical->z_start_nm, 105.0, 1e-6);
}

TEST_F(TomlRoundtripTest, DisplayOnlyLayerRoundtrips) {
    // label (last layer, index 9) should have no physical props after round-trip
    const auto& label = stack2.layers[9];
    EXPECT_FALSE(label.physical.has_value());
    EXPECT_EQ(label.name, "label");
}
