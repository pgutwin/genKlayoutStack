#include "gtest/gtest.h"
#include "gks/io/TomlReader.hpp"
#include "gks/io/ScriptWriter.hpp"
#include "gks/io/ScriptReader.hpp"
#include "gks/core/LayerStack.hpp"

#include <filesystem>
#include <string>

static const std::string kFixturesDir = GKS_FIXTURES_DIR;

class ScriptRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 1. Read TOML
        auto r = gks::readToml(kFixturesDir + "/asap7_stack.toml");
        ASSERT_TRUE(r.has_value()) << r.error().message;

        // 2. buildStack
        auto br = gks::buildStack(r->tech_name, r->version, r->layers, r->defaults);
        orig = std::move(br.stack);

        // 3. Write script
        tmp_script = "/tmp/gks_roundtrip_script_test.rb";
        auto wr = gks::writeScript(orig, tmp_script);
        ASSERT_TRUE(wr.has_value()) << wr.error().message;

        // 4. Read script back
        auto rr = gks::readScript(tmp_script);
        ASSERT_TRUE(rr.has_value()) << rr.error().message;
        raw_back = std::move(rr->layers);
    }

    void TearDown() override {
        std::filesystem::remove(tmp_script);
    }

    gks::LayerStack          orig;
    std::string              tmp_script;
    std::vector<gks::RawLayer> raw_back;
};

// Helper: find a RawLayer by (layer_num, datatype)
static const gks::RawLayer* findRaw(const std::vector<gks::RawLayer>& v, int ln, int dt) {
    for (const auto& rl : v) {
        if (rl.layer_num.has_value() && *rl.layer_num == ln &&
            rl.datatype.has_value()  && *rl.datatype  == dt) {
            return &rl;
        }
    }
    return nullptr;
}

TEST_F(ScriptRoundtripTest, PhysicalLayerCount) {
    // Count layers with physical in orig
    int phys_count = 0;
    for (const auto& e : orig.layers) {
        if (e.physical.has_value()) ++phys_count;
    }
    EXPECT_EQ(phys_count, 9);
    // raw_back should have 9 entries (label skipped)
    EXPECT_EQ(raw_back.size(), 9u);
}

TEST_F(ScriptRoundtripTest, ZStartRoundtrips) {
    for (const auto& e : orig.layers) {
        if (!e.physical.has_value()) continue;
        SCOPED_TRACE("layer " + e.name);
        const auto* rl = findRaw(raw_back, e.layer_num, e.datatype);
        ASSERT_NE(rl, nullptr) << "Layer not found in script output: " << e.name;
        ASSERT_TRUE(rl->z_start_nm.has_value());
        EXPECT_NEAR(*rl->z_start_nm, e.physical->z_start_nm, 1e-6);
    }
}

TEST_F(ScriptRoundtripTest, ThicknessRoundtrips) {
    for (const auto& e : orig.layers) {
        if (!e.physical.has_value()) continue;
        SCOPED_TRACE("layer " + e.name);
        const auto* rl = findRaw(raw_back, e.layer_num, e.datatype);
        ASSERT_NE(rl, nullptr);
        ASSERT_TRUE(rl->thickness_nm.has_value());
        EXPECT_NEAR(*rl->thickness_nm, e.physical->thickness_nm, 1e-6);
    }
}

TEST_F(ScriptRoundtripTest, DisplayOnlySkipped) {
    // label (9, 1) should NOT appear in raw_back
    const auto* rl = findRaw(raw_back, 9, 1);
    EXPECT_EQ(rl, nullptr) << "Display-only layer 'label' should not appear in script output";
}
