#include "gtest/gtest.h"
#include "gks/core/LayerStack.hpp"

#include <cmath>

using namespace gks;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static RawLayer makeRaw(int layer_num, int datatype = 0,
                        std::optional<double> z_start   = std::nullopt,
                        std::optional<double> thickness = std::nullopt,
                        std::optional<std::string> material = std::nullopt,
                        std::string name = "") {
    RawLayer r;
    r.layer_num   = layer_num;
    r.datatype    = datatype;
    r.z_start_nm  = z_start;
    r.thickness_nm = thickness;
    r.material    = material;
    r.name        = name.empty() ? std::optional<std::string>{} : std::optional<std::string>{name};
    return r;
}

static bool hasDiag(const std::vector<GksDiagnostic>& diags,
                    GksDiagnostic::Level level,
                    const std::string& substr) {
    for (const auto& d : diags) {
        if (d.level == level && d.message.find(substr) != std::string::npos)
            return true;
    }
    return false;
}

static bool approxEq(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) < eps;
}

// ─── Test 1: Sequential stack accumulates from z=0 ───────────────────────────

TEST(BuildStack, SequentialAccumulationFromZero) {
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(1, 0, std::nullopt, 50.0,  "silicon",  "diffusion"));
    raw.push_back(makeRaw(2, 0, std::nullopt, 36.0,  "metal",    "m0"));
    raw.push_back(makeRaw(3, 0, std::nullopt, 40.0,  "dielectric","ild1"));

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 3u);
    ASSERT_TRUE(layers[0].physical.has_value());
    ASSERT_TRUE(layers[1].physical.has_value());
    ASSERT_TRUE(layers[2].physical.has_value());

    // Layer 0: z_start=0, z_top=50
    EXPECT_TRUE(approxEq(layers[0].physical->z_start_nm, 0.0));
    EXPECT_TRUE(approxEq(layers[0].physical->thickness_nm, 50.0));

    // Layer 1: z_start=50, z_top=86
    EXPECT_TRUE(approxEq(layers[1].physical->z_start_nm, 50.0));
    EXPECT_TRUE(approxEq(layers[1].physical->thickness_nm, 36.0));

    // Layer 2: z_start=86, z_top=126
    EXPECT_TRUE(approxEq(layers[2].physical->z_start_nm, 86.0));
    EXPECT_TRUE(approxEq(layers[2].physical->thickness_nm, 40.0));
}

// ─── Test 2: Sequential accumulation from negative z start ───────────────────

TEST(BuildStack, SequentialAccumulationFromNegativeZ) {
    std::vector<RawLayer> raw;
    // Explicit negative start
    raw.push_back(makeRaw(1, 0, -300.0, 300.0, "silicon", "substrate"));
    // Accumulated: should start at 0.0 (high_water after layer 0)
    raw.push_back(makeRaw(2, 0, std::nullopt, 50.0, "silicon", "diffusion"));

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 2u);
    ASSERT_TRUE(layers[0].physical.has_value());
    ASSERT_TRUE(layers[1].physical.has_value());

    EXPECT_TRUE(approxEq(layers[0].physical->z_start_nm, -300.0));
    EXPECT_TRUE(approxEq(layers[0].physical->thickness_nm, 300.0));
    // high_water = max(0.0, -300+300) = 0.0
    EXPECT_TRUE(approxEq(layers[1].physical->z_start_nm, 0.0));
    EXPECT_TRUE(approxEq(layers[1].physical->thickness_nm, 50.0));
}

// ─── Test 3: Parallel group (same explicit z_start) ──────────────────────────

TEST(BuildStack, ParallelGroup) {
    std::vector<RawLayer> raw;
    // First: z_start=0, thickness=50 -> high_water = 50
    raw.push_back(makeRaw(1, 0, 0.0, 50.0, "silicon", "base"));
    // Parallel group: both share z_start=105
    raw.push_back(makeRaw(4, 0, 105.0, 45.0, "tungsten", "epi_contact"));
    raw.push_back(makeRaw(5, 0, 105.0, 45.0, "tungsten", "gate_contact"));
    // Next after parallel group: accumulated from high_water=150
    raw.push_back(makeRaw(6, 0, std::nullopt, 36.0, "metal", "m0"));

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 4u);

    // epi_contact and gate_contact both at z_start=105
    EXPECT_TRUE(approxEq(layers[1].physical->z_start_nm, 105.0));
    EXPECT_TRUE(approxEq(layers[2].physical->z_start_nm, 105.0));

    // high_water after parallel group: max(50, 105+45) = 150
    EXPECT_TRUE(approxEq(layers[3].physical->z_start_nm, 150.0));

    // Parallel group should be detected in diagnostics
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::INFO, "parallel group"));
}

// ─── Test 4: Mixed stack spanning bonding plane ───────────────────────────────

TEST(BuildStack, MixedStackSpanningBondingPlane) {
    std::vector<RawLayer> raw;
    // Backside: explicit negative z
    raw.push_back(makeRaw(1, 0, -300.0, 300.0, "silicon",  "substrate"));
    // Above bonding plane: accumulated
    raw.push_back(makeRaw(2, 0, std::nullopt, 50.0, "silicon", "diffusion"));
    raw.push_back(makeRaw(3, 0, std::nullopt, 36.0, "metal",   "m0"));

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 3u);

    EXPECT_TRUE(approxEq(layers[0].physical->z_start_nm, -300.0));
    // high_water after layer 0: max(0.0, -300+300) = 0.0
    EXPECT_TRUE(approxEq(layers[1].physical->z_start_nm, 0.0));
    // high_water after layer 1: max(0.0, 0+50) = 50
    EXPECT_TRUE(approxEq(layers[2].physical->z_start_nm, 50.0));
}

// ─── Test 5: Burial warning ───────────────────────────────────────────────────

TEST(BuildStack, BurialWarning) {
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(1, 0, 0.0,  100.0, "silicon", "base"));
    // z_start=50 is below high_water=100 — burial
    raw.push_back(makeRaw(2, 0, 50.0,  36.0, "metal",   "buried"));

    auto result = buildStack("TEST", "1.0", raw);

    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "below high_water"));
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "burial"));
}

// ─── Test 6: Gap warning ──────────────────────────────────────────────────────

TEST(BuildStack, GapWarning) {
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(1, 0, 0.0,   50.0, "silicon", "base"));
    // z_start=200 is above high_water=50 — gap
    raw.push_back(makeRaw(2, 0, 200.0, 36.0, "metal",   "floating"));

    auto result = buildStack("TEST", "1.0", raw);

    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "gap above high_water"));
}

// ─── Test 7: Document-level default thickness applied ────────────────────────

TEST(BuildStack, DefaultThicknessApplied) {
    LayerStack::Defaults defaults;
    defaults.thickness_nm = 36.0;

    std::vector<RawLayer> raw;
    // No thickness specified — should use default
    RawLayer r;
    r.layer_num = 1;
    r.datatype  = 0;
    r.name      = "m0";
    r.material  = "metal";
    // z_start_nm absent, thickness_nm absent -> has_physical due to material
    raw.push_back(r);

    auto result = buildStack("TEST", "1.0", raw, defaults);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 1u);
    ASSERT_TRUE(layers[0].physical.has_value());
    EXPECT_TRUE(approxEq(layers[0].physical->thickness_nm, 36.0));
}

// ─── Test 8: All-display-only stack ──────────────────────────────────────────

TEST(BuildStack, AllDisplayOnly) {
    std::vector<RawLayer> raw;
    // No physical fields — display-only
    for (int i = 0; i < 3; ++i) {
        RawLayer r;
        r.layer_num = i;
        r.datatype  = 0;
        r.name      = "layer" + std::to_string(i);
        // fill_color set — display only
        r.fill_color = Color{0x80, 0x80, 0x80};
        raw.push_back(r);
    }

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 3u);
    for (const auto& layer : layers) {
        EXPECT_FALSE(layer.physical.has_value())
            << "Expected display-only layer to have no physical props";
    }
}

// ─── Test 9: layer_expression passes through to PhysicalProps ────────────────

TEST(BuildStack, LayerExpressionPassthrough) {
    std::vector<RawLayer> raw;
    RawLayer r;
    r.layer_num       = 4;
    r.datatype        = 0;
    r.name            = "contacts";
    r.z_start_nm      = 105.0;
    r.thickness_nm    = 45.0;
    r.material        = "tungsten";
    r.layer_expression = "input(4,0) + input(5,0)";
    raw.push_back(r);

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 1u);
    ASSERT_TRUE(layers[0].physical.has_value());
    ASSERT_TRUE(layers[0].physical->layer_expression.has_value());
    EXPECT_EQ(*layers[0].physical->layer_expression, "input(4,0) + input(5,0)");
}
