#include "gtest/gtest.h"
#include "gks/core/LayerStack.hpp"
#include "gks/core/Validator.hpp"
#include "gks/io/LypReader.hpp"
#include "gks/io/LypWriter.hpp"
#include "gks/io/ScriptReader.hpp"
#include "gks/io/ScriptWriter.hpp"
#include "gks/io/TomlReader.hpp"
#include "gks/io/TomlWriter.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>

static const std::string kFixturesDir = GKS_FIXTURES_DIR;

// ─── Helper: simulate gks import --lyp <lyp> --script <rb> ───────────────────
//
// Returns the merged LayerStack:
//   - All layers from .lyp (display props from lyp, physical = nullopt initially)
//   - Script layers matched by (layer_num, datatype) get their physical props merged in
//   - Script name: option populates LayerEntry.name
//   - Unmatched script layers are skipped (logged internally)
//
static gks::LayerStack simulateImport(
    const std::string& lyp_path,
    const std::string& script_path,
    bool prefer_script_colors = false)
{
    gks::LayerStack stack;

    if (!lyp_path.empty()) {
        auto lr = gks::readLyp(lyp_path);
        EXPECT_TRUE(lr.has_value()) << (lr.has_value() ? "" : lr.error().message);
        if (lr.has_value()) {
            stack = std::move(*lr);
        }
    }

    if (!script_path.empty()) {
        auto sr = gks::readScript(script_path);
        EXPECT_TRUE(sr.has_value()) << (sr.has_value() ? "" : sr.error().message);
        if (!sr.has_value()) return stack;

        if (!lyp_path.empty()) {
            // Merge script data into lyp-derived stack
            for (const auto& raw : sr->layers) {
                int ln = raw.layer_num.value_or(-1);
                int dt = raw.datatype.value_or(0);

                auto it = std::find_if(stack.layers.begin(), stack.layers.end(),
                    [&](const gks::LayerEntry& e) {
                        return e.layer_num == ln && e.datatype == dt;
                    });

                if (it == stack.layers.end()) {
                    // Unmatched script layer — skip (warn in real CLI)
                    continue;
                }

                if (raw.name.has_value() && !raw.name->empty()) {
                    it->name = *raw.name;
                }
                if (prefer_script_colors) {
                    if (raw.fill_color)  it->display.fill_color  = *raw.fill_color;
                    if (raw.frame_color) it->display.frame_color = *raw.frame_color;
                }
                if (raw.z_start_nm.has_value() || raw.thickness_nm.has_value()
                        || raw.material.has_value()) {
                    gks::PhysicalProps phys;
                    phys.z_start_nm   = raw.z_start_nm.value_or(0.0);
                    phys.thickness_nm = raw.thickness_nm.value_or(0.0);
                    phys.material     = raw.material.value_or("");
                    phys.layer_expression = raw.layer_expression;
                    it->physical = phys;
                }
            }
        } else {
            // Script-only import: build stack from script layers
            for (const auto& raw : sr->layers) {
                gks::LayerEntry entry;
                entry.layer_num = raw.layer_num.value_or(0);
                entry.datatype  = raw.datatype.value_or(0);
                entry.name      = raw.name.value_or("");
                entry.purpose   = raw.purpose.value_or("drawing");
                if (raw.fill_color)  entry.display.fill_color  = *raw.fill_color;
                if (raw.frame_color) entry.display.frame_color = *raw.frame_color;
                if (raw.z_start_nm.has_value() || raw.thickness_nm.has_value()
                        || raw.material.has_value()) {
                    gks::PhysicalProps phys;
                    phys.z_start_nm   = raw.z_start_nm.value_or(0.0);
                    phys.thickness_nm = raw.thickness_nm.value_or(0.0);
                    phys.material     = raw.material.value_or("");
                    phys.layer_expression = raw.layer_expression;
                    entry.physical = phys;
                }
                stack.layers.push_back(std::move(entry));
            }
        }
    }

    return stack;
}

// ─── Fixture: bootstrap import of default.lyp + asap7_25d.rb ─────────────────

class BootstrapImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        lyp_path    = kFixturesDir + "/default.lyp";
        script_path = kFixturesDir + "/asap7_25d.rb";
        merged_toml = "/tmp/gks_bootstrap_merged.toml";
        out_lyp     = "/tmp/gks_bootstrap_out.lyp";
        out_rb      = "/tmp/gks_bootstrap_out.rb";

        // Simulate: gks import --lyp default.lyp --script asap7_25d.rb -o merged.toml
        merged = simulateImport(lyp_path, script_path);

        // Write merged TOML
        auto wr = gks::writeToml(merged, merged_toml);
        ASSERT_TRUE(wr.has_value()) << wr.error().message;

        // Read merged TOML back and rebuild stack
        auto tr = gks::readToml(merged_toml);
        ASSERT_TRUE(tr.has_value()) << tr.error().message;
        auto br = gks::buildStack(tr->tech_name, tr->version, tr->layers, tr->defaults);
        merged_back = std::move(br.stack);
    }

    void TearDown() override {
        std::filesystem::remove(merged_toml);
        std::filesystem::remove(out_lyp);
        std::filesystem::remove(out_rb);
    }

    std::string     lyp_path;
    std::string     script_path;
    std::string     merged_toml;
    std::string     out_lyp;
    std::string     out_rb;
    gks::LayerStack merged;
    gks::LayerStack merged_back;
};

// ─── Test: merged TOML has all layers from default.lyp ───────────────────────

TEST_F(BootstrapImportTest, MergedLayerCount) {
    // default.lyp has 43 layers; they should all be in the merged stack
    EXPECT_EQ(merged.layers.size(), 43u);
}

// ─── Test: layers in both lyp and script have physical props ─────────────────
//
// default.lyp has layers 1/0 and 2/0.
// asap7_25d.rb has z() entries for input(1, 0) and input(2, 0).
// After merge, those two layers should carry physical props.

TEST_F(BootstrapImportTest, MatchedLayerHasPhysical_Layer1) {
    auto it = std::find_if(merged.layers.begin(), merged.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 1 && e.datatype == 0; });
    ASSERT_NE(it, merged.layers.end()) << "Layer 1/0 not found in merged stack";
    ASSERT_TRUE(it->physical.has_value()) << "Layer 1/0 should have physical props from script";
    EXPECT_NEAR(it->physical->z_start_nm,   -150.0, 1e-6);
    EXPECT_NEAR(it->physical->thickness_nm,  100.0, 1e-6);
}

TEST_F(BootstrapImportTest, MatchedLayerHasPhysical_Layer2) {
    auto it = std::find_if(merged.layers.begin(), merged.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 2 && e.datatype == 0; });
    ASSERT_NE(it, merged.layers.end()) << "Layer 2/0 not found in merged stack";
    ASSERT_TRUE(it->physical.has_value()) << "Layer 2/0 should have physical props from script";
    EXPECT_NEAR(it->physical->z_start_nm,   0.0, 1e-6);
    EXPECT_NEAR(it->physical->thickness_nm, 50.0, 1e-6);
}

// ─── Test: lyp-only layers have display but no physical ──────────────────────

TEST_F(BootstrapImportTest, UnmatchedLypLayerDisplayOnly) {
    // Layer 9/0 is in default.lyp but NOT in asap7_25d.rb
    auto it = std::find_if(merged.layers.begin(), merged.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 9 && e.datatype == 0; });
    ASSERT_NE(it, merged.layers.end()) << "Layer 9/0 not found in merged stack";
    EXPECT_FALSE(it->physical.has_value()) << "Layer 9/0 should have no physical props";
}

// ─── Test: display props from lyp preserved for matched layers ────────────────

TEST_F(BootstrapImportTest, DisplayPropsFromLypPreserved) {
    // Layer 1/0 in default.lyp has fill-color=#ff80a8
    auto it = std::find_if(merged.layers.begin(), merged.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 1 && e.datatype == 0; });
    ASSERT_NE(it, merged.layers.end());
    auto expected = gks::Color::fromHex("#ff80a8");
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(it->display.fill_color, *expected);
}

// ─── Test: name populated from script ────────────────────────────────────────

TEST_F(BootstrapImportTest, NameFromScript_Layer1) {
    // asap7_25d.rb: z(input(1, 0), ..., name: "bspdn")
    auto it = std::find_if(merged.layers.begin(), merged.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 1 && e.datatype == 0; });
    ASSERT_NE(it, merged.layers.end());
    EXPECT_EQ(it->name, "bspdn");
}

TEST_F(BootstrapImportTest, NameFromScript_Layer2) {
    // asap7_25d.rb: z(input(2, 0), ..., name: "diffusion")
    auto it = std::find_if(merged.layers.begin(), merged.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 2 && e.datatype == 0; });
    ASSERT_NE(it, merged.layers.end());
    EXPECT_EQ(it->name, "diffusion");
}

// ─── Test: TOML round-trip preserves layer count and physical presence ────────

TEST_F(BootstrapImportTest, TomlRoundtripLayerCount) {
    EXPECT_EQ(merged_back.layers.size(), merged.layers.size());
}

TEST_F(BootstrapImportTest, TomlRoundtripPhysicalPresence) {
    for (size_t i = 0; i < merged.layers.size(); ++i) {
        SCOPED_TRACE("layer index " + std::to_string(i));
        EXPECT_EQ(merged_back.layers[i].layer_num, merged.layers[i].layer_num);
        EXPECT_EQ(merged_back.layers[i].datatype,  merged.layers[i].datatype);
        EXPECT_EQ(merged_back.layers[i].physical.has_value(),
                  merged.layers[i].physical.has_value());
    }
}

TEST_F(BootstrapImportTest, TomlRoundtripPhysicalValues) {
    // Verify physical fields survive TOML round-trip for matched layers
    auto it1 = std::find_if(merged_back.layers.begin(), merged_back.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 1 && e.datatype == 0; });
    ASSERT_NE(it1, merged_back.layers.end());
    ASSERT_TRUE(it1->physical.has_value());
    EXPECT_NEAR(it1->physical->z_start_nm,   -150.0, 1e-6);
    EXPECT_NEAR(it1->physical->thickness_nm,  100.0, 1e-6);

    auto it2 = std::find_if(merged_back.layers.begin(), merged_back.layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 2 && e.datatype == 0; });
    ASSERT_NE(it2, merged_back.layers.end());
    ASSERT_TRUE(it2->physical.has_value());
    EXPECT_NEAR(it2->physical->z_start_nm,   0.0, 1e-6);
    EXPECT_NEAR(it2->physical->thickness_nm, 50.0, 1e-6);
}

// ─── Test: generate from merged stack ────────────────────────────────────────
//
// Simulates: gks generate merged.toml --lyp out.lyp --script out.rb
// Verify generated .lyp has correct display props
// Verify generated .rb has correct physical props for matched layers

TEST_F(BootstrapImportTest, GenerateFromMerged_LypLayerCount) {
    auto wr = gks::writeLyp(merged_back, out_lyp);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto lr = gks::readLyp(out_lyp);
    ASSERT_TRUE(lr.has_value()) << lr.error().message;

    // Generated .lyp should have same number of layers as merged stack
    EXPECT_EQ(lr->layers.size(), merged_back.layers.size());
}

TEST_F(BootstrapImportTest, GenerateFromMerged_LypDisplayProps) {
    auto wr = gks::writeLyp(merged_back, out_lyp);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto lr = gks::readLyp(out_lyp);
    ASSERT_TRUE(lr.has_value()) << lr.error().message;

    // Layer 1/0 display props preserved from default.lyp through generate
    auto it = std::find_if(lr->layers.begin(), lr->layers.end(),
        [](const gks::LayerEntry& e) { return e.layer_num == 1 && e.datatype == 0; });
    ASSERT_NE(it, lr->layers.end());
    auto expected = gks::Color::fromHex("#ff80a8");
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(it->display.fill_color, *expected);
}

TEST_F(BootstrapImportTest, GenerateFromMerged_ScriptPhysicalLayers) {
    auto wr = gks::writeScript(merged_back, out_rb);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto sr = gks::readScript(out_rb);
    ASSERT_TRUE(sr.has_value()) << sr.error().message;

    // Script should contain exactly the 2 physical layers (1/0 and 2/0)
    EXPECT_EQ(sr->layers.size(), 2u);
}

TEST_F(BootstrapImportTest, GenerateFromMerged_ScriptPhysicalValues) {
    auto wr = gks::writeScript(merged_back, out_rb);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto sr = gks::readScript(out_rb);
    ASSERT_TRUE(sr.has_value()) << sr.error().message;

    // Layer 1/0 physical props from script preserved through generate
    auto it = std::find_if(sr->layers.begin(), sr->layers.end(),
        [](const gks::RawLayer& r) {
            return r.layer_num.value_or(-1) == 1 && r.datatype.value_or(-1) == 0;
        });
    ASSERT_NE(it, sr->layers.end()) << "Layer 1/0 not found in generated script";
    ASSERT_TRUE(it->z_start_nm.has_value());
    EXPECT_NEAR(*it->z_start_nm, -150.0, 1e-6);
    ASSERT_TRUE(it->thickness_nm.has_value());
    EXPECT_NEAR(*it->thickness_nm, 100.0, 1e-6);
}
