#include "gtest/gtest.h"
#include "gks/io/TomlReader.hpp"
#include "gks/core/LayerStack.hpp"

#include <string>

// GKS_FIXTURES_DIR is defined by CMake as a compile-time constant
static const std::string kFixturesDir = GKS_FIXTURES_DIR;

// ── TomlReader: parse asap7_stack.toml ─────────────────────────────────────

class TomlReaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto result = gks::readToml(kFixturesDir + "/asap7_stack.toml");
        ASSERT_TRUE(result.has_value()) << result.error().message;
        res = std::move(*result);
    }
    gks::TomlReadResult res;
};

TEST_F(TomlReaderTest, StackMetadata) {
    EXPECT_EQ(res.tech_name, "ASAP7");
    EXPECT_EQ(res.version, "1.0.0");
}

TEST_F(TomlReaderTest, LayerCount) {
    // 9 physical layers + 1 display-only = 10
    EXPECT_EQ(res.layers.size(), 10u);
}

TEST_F(TomlReaderTest, Defaults) {
    ASSERT_TRUE(res.defaults.thickness_nm.has_value());
    EXPECT_DOUBLE_EQ(*res.defaults.thickness_nm, 36.0);

    ASSERT_TRUE(res.defaults.material.has_value());
    EXPECT_EQ(*res.defaults.material, "dielectric");

    ASSERT_TRUE(res.defaults.dither_pattern.has_value());
    EXPECT_EQ(*res.defaults.dither_pattern, 9);

    ASSERT_TRUE(res.defaults.line_style.has_value());
    EXPECT_EQ(*res.defaults.line_style, -1);

    ASSERT_TRUE(res.defaults.fill_alpha.has_value());
    EXPECT_EQ(*res.defaults.fill_alpha, 128);
}

TEST_F(TomlReaderTest, SubstrateLayerFields) {
    // Layer 0 = substrate
    const auto& raw = res.layers[0];
    ASSERT_TRUE(raw.layer_num.has_value());
    EXPECT_EQ(*raw.layer_num, 0);
    ASSERT_TRUE(raw.datatype.has_value());
    EXPECT_EQ(*raw.datatype, 0);
    ASSERT_TRUE(raw.name.has_value());
    EXPECT_EQ(*raw.name, "substrate");
    ASSERT_TRUE(raw.purpose.has_value());
    EXPECT_EQ(*raw.purpose, "drawing");

    // Physical props
    ASSERT_TRUE(raw.z_start_nm.has_value());
    EXPECT_DOUBLE_EQ(*raw.z_start_nm, -300.0);
    ASSERT_TRUE(raw.thickness_nm.has_value());
    EXPECT_DOUBLE_EQ(*raw.thickness_nm, 300.0);
    ASSERT_TRUE(raw.material.has_value());
    EXPECT_EQ(*raw.material, "silicon");

    // Display props
    ASSERT_TRUE(raw.fill_color.has_value());
    EXPECT_EQ(raw.fill_color->toHex(), "#888888");
    ASSERT_TRUE(raw.frame_color.has_value());
    EXPECT_EQ(raw.frame_color->toHex(), "#444444");
    ASSERT_TRUE(raw.dither_pattern.has_value());
    EXPECT_EQ(*raw.dither_pattern, 9);
}

TEST_F(TomlReaderTest, BacksideLayerNegativeZ) {
    // Layer 1 = bspdn (second backside layer)
    const auto& raw = res.layers[1];
    ASSERT_TRUE(raw.z_start_nm.has_value());
    EXPECT_DOUBLE_EQ(*raw.z_start_nm, -150.0);
    ASSERT_TRUE(raw.thickness_nm.has_value());
    EXPECT_DOUBLE_EQ(*raw.thickness_nm, 100.0);
}

TEST_F(TomlReaderTest, AccumulatedLayerHasNoZStart) {
    // Layer 2 = diffusion — no explicit z_start_nm
    const auto& raw = res.layers[2];
    EXPECT_FALSE(raw.z_start_nm.has_value());
    ASSERT_TRUE(raw.thickness_nm.has_value());
    EXPECT_DOUBLE_EQ(*raw.thickness_nm, 50.0);
}

TEST_F(TomlReaderTest, DefaultThicknessLayerHasNoThickness) {
    // Layer 3 = poly — no explicit thickness_nm (uses document default)
    const auto& raw = res.layers[3];
    EXPECT_FALSE(raw.z_start_nm.has_value());
    EXPECT_FALSE(raw.thickness_nm.has_value());
    ASSERT_TRUE(raw.material.has_value());
    EXPECT_EQ(*raw.material, "polysilicon");
}

TEST_F(TomlReaderTest, ParallelGroupBothHaveSameZStart) {
    // Layers 4 and 5 = epi_contact and gate_contact — parallel group
    const auto& epi  = res.layers[4];
    const auto& gate = res.layers[5];

    ASSERT_TRUE(epi.z_start_nm.has_value());
    ASSERT_TRUE(gate.z_start_nm.has_value());
    EXPECT_DOUBLE_EQ(*epi.z_start_nm, 105.0);
    EXPECT_DOUBLE_EQ(*gate.z_start_nm, 105.0);

    EXPECT_EQ(*epi.name,  "epi_contact");
    EXPECT_EQ(*gate.name, "gate_contact");
}

TEST_F(TomlReaderTest, M0LayerNoZStartNoThickness) {
    // Layer 6 = m0 — no z_start, no thickness (uses default)
    const auto& raw = res.layers[6];
    EXPECT_FALSE(raw.z_start_nm.has_value());
    EXPECT_FALSE(raw.thickness_nm.has_value());
    ASSERT_TRUE(raw.name.has_value());
    EXPECT_EQ(*raw.name, "m0");
}

TEST_F(TomlReaderTest, DisplayOnlyLayerHasNoPhysical) {
    // Layer 9 = label — display only
    const auto& raw = res.layers[9];
    EXPECT_FALSE(raw.z_start_nm.has_value());
    EXPECT_FALSE(raw.thickness_nm.has_value());
    EXPECT_FALSE(raw.material.has_value());
    ASSERT_TRUE(raw.name.has_value());
    EXPECT_EQ(*raw.name, "label");
    ASSERT_TRUE(raw.layer_num.has_value());
    EXPECT_EQ(*raw.layer_num, 9);
    ASSERT_TRUE(raw.datatype.has_value());
    EXPECT_EQ(*raw.datatype, 1);
}

TEST_F(TomlReaderTest, SourceLineIsPopulated) {
    // Each raw layer should have a non-zero source_line
    for (const auto& raw : res.layers) {
        EXPECT_GT(raw.source_line, 0)
            << "Expected non-zero source_line for layer "
            << raw.layer_num.value_or(-1);
    }
}

// ── Error handling ──────────────────────────────────────────────────────────

TEST(TomlReaderError, MissingFileFails) {
    auto result = gks::readToml("/tmp/gks_nonexistent_file.toml");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().message.empty());
}
