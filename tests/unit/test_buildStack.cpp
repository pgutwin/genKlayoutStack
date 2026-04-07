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
    // First [[layer]] = top of stack; buildStack reverses before accumulation.
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(3, 0, std::nullopt, 40.0,  "dielectric","ild1"));   // top
    raw.push_back(makeRaw(2, 0, std::nullopt, 36.0,  "metal",    "m0"));
    raw.push_back(makeRaw(1, 0, std::nullopt, 50.0,  "silicon",  "diffusion")); // bottom

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
    // First [[layer]] = top; substrate is last (bottom).
    std::vector<RawLayer> raw;
    // Accumulated: will start at 0.0 (high_water after substrate)
    raw.push_back(makeRaw(2, 0, std::nullopt, 50.0, "silicon", "diffusion")); // top
    // Explicit negative start — processed first after reversal
    raw.push_back(makeRaw(1, 0, -300.0, 300.0, "silicon", "substrate"));     // bottom

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
    // First [[layer]] = top; buildStack reverses before accumulation.
    std::vector<RawLayer> raw;
    // Top: accumulated from high_water=150 after parallel group
    raw.push_back(makeRaw(6, 0, std::nullopt, 36.0, "metal", "m0"));
    // Parallel group: both share z_start=105
    raw.push_back(makeRaw(5, 0, 105.0, 45.0, "tungsten", "gate_contact"));
    raw.push_back(makeRaw(4, 0, 105.0, 45.0, "tungsten", "epi_contact"));
    // Bottom: z_start=0, thickness=50 -> high_water = 50
    raw.push_back(makeRaw(1, 0, 0.0, 50.0, "silicon", "base"));

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
    // First [[layer]] = top; substrate is last (processed first after reversal).
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(3, 0, std::nullopt, 36.0, "metal",   "m0"));       // top
    raw.push_back(makeRaw(2, 0, std::nullopt, 50.0, "silicon", "diffusion"));
    // Backside: explicit negative z — processed first after reversal
    raw.push_back(makeRaw(1, 0, -300.0, 300.0, "silicon",  "substrate"));    // bottom

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
    // First [[layer]] = top; reversed before accumulation.
    std::vector<RawLayer> raw;
    // z_start=50 is below high_water=100 after base is processed — burial
    raw.push_back(makeRaw(2, 0, 50.0,  36.0, "metal",   "buried")); // top
    raw.push_back(makeRaw(1, 0, 0.0,  100.0, "silicon", "base"));   // bottom

    auto result = buildStack("TEST", "1.0", raw);

    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "below high_water"));
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "burial"));
}

// ─── Test 6: Gap warning ──────────────────────────────────────────────────────

TEST(BuildStack, GapWarning) {
    // First [[layer]] = top; reversed before accumulation.
    std::vector<RawLayer> raw;
    // z_start=200 is above high_water=50 after base is processed — gap
    raw.push_back(makeRaw(2, 0, 200.0, 36.0, "metal",   "floating")); // top
    raw.push_back(makeRaw(1, 0, 0.0,   50.0, "silicon", "base"));     // bottom

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

// ═════════════════════════════════════════════════════════════════════════════
// v0.4.2 — two-pass accumulation + alignment resolution
// ═════════════════════════════════════════════════════════════════════════════

// ─── Test 10: Anchor in the middle of the file ────────────────────────────────
// Layers above anchor → positive z (Pass 1).
// Layers below anchor → negative z (Pass 2).

TEST(BuildStack, AnchorInMiddle) {
    // File order: m1 (top), poly (anchor, z=0), substrate (bottom)
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(3, 0, std::nullopt, 36.0,  "metal",   "m1"));       // above anchor
    raw.push_back(makeRaw(2, 0, 0.0,         22.0,  "poly",    "poly"));      // anchor
    raw.push_back(makeRaw(1, 0, std::nullopt, 300.0, "silicon", "substrate")); // below anchor

    auto result = buildStack("TEST", "1.0", raw);
    ASSERT_EQ(result.diagnostics.size() > 0u, true); // at least the anchor INFO
    const auto& layers = result.stack.layers;
    ASSERT_EQ(layers.size(), 3u);

    // After sort: substrate(-300), poly(0), m1(22)
    EXPECT_TRUE(approxEq(layers[0].physical->z_start_nm, -300.0)); // substrate
    EXPECT_TRUE(approxEq(layers[1].physical->z_start_nm,    0.0)); // poly (anchor)
    EXPECT_TRUE(approxEq(layers[2].physical->z_start_nm,   22.0)); // m1

    // Anchor summary diagnostic should be emitted
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::INFO, "Anchor layer"));
}

// ─── Test 11: No anchor — backward compatibility ──────────────────────────────
// No layer has z_start_nm = 0.0 explicitly. The last layer becomes the implicit
// anchor at z=0, and the whole stack accumulates upward: identical to v0.4.1.

TEST(BuildStack, NoAnchorBackwardCompat) {
    // File order: m0 (top), diffusion (bottom) — no explicit z=0 on either
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(2, 0, std::nullopt, 36.0, "metal",   "m0"));       // top
    raw.push_back(makeRaw(1, 0, std::nullopt, 50.0, "silicon", "diffusion")); // bottom (anchor)

    auto result = buildStack("TEST", "1.0", raw);
    const auto& layers = result.stack.layers;

    ASSERT_EQ(layers.size(), 2u);
    // diffusion (last in file) → implicit anchor at z=0
    EXPECT_TRUE(approxEq(layers[0].physical->z_start_nm,  0.0)); // diffusion
    EXPECT_TRUE(approxEq(layers[1].physical->z_start_nm, 50.0)); // m0
    // No anchor summary (anchor_found=false)
    EXPECT_FALSE(hasDiag(result.diagnostics, GksDiagnostic::Level::INFO, "Anchor layer"));
}

// ─── Test 12: align_bottom_to — parallel group via alignment ─────────────────
// Two layers both snap their bottom to poly:top → parallel group at z=22.

TEST(BuildStack, AlignBottomTo) {
    // File order (top to bottom): m0, epi, gate, poly (anchor), substrate
    std::vector<RawLayer> raw;

    RawLayer m0;
    m0.layer_num = 6; m0.datatype = 0; m0.name = "m0";
    m0.thickness_nm = 36.0; m0.material = "metal";
    // no z_start → accumulated upward from hw after parallel group
    raw.push_back(m0);

    RawLayer epi;
    epi.layer_num = 4; epi.datatype = 0; epi.name = "epi_contact";
    epi.align_bottom_to = "poly:top";
    epi.thickness_nm = 45.0; epi.material = "tungsten";
    raw.push_back(epi);

    RawLayer gate;
    gate.layer_num = 5; gate.datatype = 0; gate.name = "gate_contact";
    gate.align_bottom_to = "poly:top"; // same → parallel group
    gate.thickness_nm = 30.0; gate.material = "tungsten";
    raw.push_back(gate);

    raw.push_back(makeRaw(3, 0, 0.0,         22.0,  "poly",    "poly"));      // anchor
    raw.push_back(makeRaw(1, 0, std::nullopt, 300.0, "silicon", "substrate")); // below

    auto result = buildStack("TEST", "1.0", raw);
    ASSERT_TRUE(result.diagnostics.empty() == false);
    const auto& layers = result.stack.layers;
    ASSERT_EQ(layers.size(), 5u);

    // Both epi_contact and gate_contact must resolve to poly:top = 0+22 = 22
    bool found_epi = false, found_gate = false;
    for (const auto& le : layers) {
        if (le.name == "epi_contact") {
            EXPECT_TRUE(approxEq(le.physical->z_start_nm, 22.0));
            found_epi = true;
        }
        if (le.name == "gate_contact") {
            EXPECT_TRUE(approxEq(le.physical->z_start_nm, 22.0));
            found_gate = true;
        }
    }
    EXPECT_TRUE(found_epi);
    EXPECT_TRUE(found_gate);

    // Parallel group should be detected
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::INFO, "parallel group"));
}

// ─── Test 13: align_top_to — snap layer top to a reference edge ───────────────
// gate_oxide sits just below poly: its top snaps to poly:bottom.
// poly:bottom = 0 → gate_oxide.z_start = 0 - 5 = -5

TEST(BuildStack, AlignTopTo) {
    // File order: poly (anchor), gate_oxide (below anchor)
    std::vector<RawLayer> raw;
    raw.push_back(makeRaw(3, 0, 0.0, 22.0, "poly", "poly")); // anchor

    RawLayer gox;
    gox.layer_num = 2; gox.datatype = 0; gox.name = "gate_oxide";
    gox.align_top_to = "poly:bottom"; // top snaps to poly's bottom edge (z=0)
    gox.thickness_nm = 5.0; gox.material = "oxide";
    raw.push_back(gox);

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_TRUE(result.diagnostics.size() > 0u);

    bool found = false;
    for (const auto& le : result.stack.layers) {
        if (le.name == "gate_oxide") {
            EXPECT_TRUE(approxEq(le.physical->z_start_nm, -5.0));
            EXPECT_TRUE(approxEq(le.physical->thickness_nm, 5.0));
            found = true;
        }
    }
    EXPECT_TRUE(found) << "gate_oxide layer not found in result";
}

// ─── Test 14: Both alignments — thickness derived from span ───────────────────
// gate_oxide: bottom snaps to poly:bottom (z=0), top snaps to m0:bottom (z=8).
// Derived thickness = 8 - 0 = 8. No thickness_nm given → no warning.

TEST(BuildStack, BothAlignmentsThicknessDerived) {
    // poly (anchor, z=0, t=22), m0 (z=30, t=10), gate_oxide (aligned between)
    // File order: m0, gate_oxide, poly (anchor)
    std::vector<RawLayer> raw;

    RawLayer m0_r;
    m0_r.layer_num = 5; m0_r.datatype = 0; m0_r.name = "m0";
    m0_r.z_start_nm = 30.0; m0_r.thickness_nm = 10.0; m0_r.material = "metal";
    raw.push_back(m0_r);

    RawLayer gox;
    gox.layer_num = 2; gox.datatype = 0; gox.name = "gate_oxide";
    gox.align_bottom_to = "poly:top";   // bottom = poly:top = 0+22 = 22
    gox.align_top_to    = "m0:bottom";  // top    = m0:bottom = 30
    // No thickness_nm → derived = 30 - 22 = 8
    gox.material = "oxide";
    raw.push_back(gox);

    raw.push_back(makeRaw(3, 0, 0.0, 22.0, "poly", "poly")); // anchor

    auto result = buildStack("TEST", "1.0", raw);
    // No errors, no thickness-mismatch warning
    EXPECT_FALSE(hasDiag(result.diagnostics, GksDiagnostic::Level::ERROR, ""));
    EXPECT_FALSE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "thickness_nm"));

    bool found = false;
    for (const auto& le : result.stack.layers) {
        if (le.name == "gate_oxide") {
            EXPECT_TRUE(approxEq(le.physical->z_start_nm,  22.0));
            EXPECT_TRUE(approxEq(le.physical->thickness_nm, 8.0));
            found = true;
        }
    }
    EXPECT_TRUE(found) << "gate_oxide not found";
}

// ─── Test 15: Both alignments — thickness mismatch emits WARN ────────────────

TEST(BuildStack, BothAlignmentsThicknessMismatch) {
    // Same as above but gate_oxide specifies thickness_nm=3.0 (derived is 8.0 → mismatch).
    std::vector<RawLayer> raw;

    RawLayer m0_r;
    m0_r.layer_num = 5; m0_r.datatype = 0; m0_r.name = "m0";
    m0_r.z_start_nm = 30.0; m0_r.thickness_nm = 10.0; m0_r.material = "metal";
    raw.push_back(m0_r);

    RawLayer gox;
    gox.layer_num = 2; gox.datatype = 0; gox.name = "gate_oxide";
    gox.align_bottom_to = "poly:top";
    gox.align_top_to    = "m0:bottom";
    gox.thickness_nm    = 3.0; // mismatch: derived = 8, given = 3
    gox.material = "oxide";
    raw.push_back(gox);

    raw.push_back(makeRaw(3, 0, 0.0, 22.0, "poly", "poly"));

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::WARN, "thickness_nm"));
    // Derived thickness (8) is still used despite the warning
    for (const auto& le : result.stack.layers) {
        if (le.name == "gate_oxide") {
            EXPECT_TRUE(approxEq(le.physical->thickness_nm, 8.0));
        }
    }
}

// ─── Test 16: align_* + z_start_nm → ERROR ───────────────────────────────────

TEST(BuildStack, AlignAndZStartError) {
    std::vector<RawLayer> raw;

    RawLayer bad;
    bad.layer_num = 1; bad.datatype = 0; bad.name = "bad_layer";
    bad.align_bottom_to = "poly:top";
    bad.z_start_nm = 50.0; // conflict
    bad.thickness_nm = 10.0; bad.material = "metal";
    raw.push_back(bad);

    raw.push_back(makeRaw(2, 0, 0.0, 22.0, "poly", "poly"));

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::ERROR, "mutually exclusive"));
}

// ─── Test 17: Circular alignment reference → ERROR ────────────────────────────

TEST(BuildStack, CircularReferenceError) {
    std::vector<RawLayer> raw;

    RawLayer a;
    a.layer_num = 1; a.datatype = 0; a.name = "layerA";
    a.align_bottom_to = "layerB:top";
    a.thickness_nm = 10.0; a.material = "metal";
    raw.push_back(a);

    RawLayer b;
    b.layer_num = 2; b.datatype = 0; b.name = "layerB";
    b.align_bottom_to = "layerA:top"; // A → B → A
    b.thickness_nm = 10.0; b.material = "metal";
    raw.push_back(b);

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::ERROR, "circular"));
}

// ─── Test 18: Unknown alignment reference → ERROR ─────────────────────────────

TEST(BuildStack, UnknownReferenceError) {
    std::vector<RawLayer> raw;

    RawLayer a;
    a.layer_num = 1; a.datatype = 0; a.name = "layerA";
    a.align_bottom_to = "nonexistent:top";
    a.thickness_nm = 10.0; a.material = "metal";
    raw.push_back(a);

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_TRUE(hasDiag(result.diagnostics, GksDiagnostic::Level::ERROR, "not found in stack"));
}

// ─── Test 19: Alignment reference to a layer in the upward pass ───────────────
// m1 aligns its bottom to m0:top; m0 has explicit z=50 (pre-resolved).
// Both m1 and m0 are in the upward region (above the anchor poly).

TEST(BuildStack, AlignmentInUpwardPass) {
    // File order: m1 (aligned), m0 (explicit z), poly (anchor), substrate
    std::vector<RawLayer> raw;

    RawLayer m1;
    m1.layer_num = 4; m1.datatype = 0; m1.name = "m1";
    m1.align_bottom_to = "m0:top"; // m0:top = 50 + 10 = 60
    m1.thickness_nm = 36.0; m1.material = "metal";
    raw.push_back(m1);

    raw.push_back(makeRaw(3, 0, 50.0, 10.0, "metal",   "m0"));       // explicit z=50
    raw.push_back(makeRaw(2, 0,  0.0, 22.0, "poly",    "poly"));     // anchor
    raw.push_back(makeRaw(1, 0, std::nullopt, 300.0, "silicon", "substrate")); // below

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_FALSE(hasDiag(result.diagnostics, GksDiagnostic::Level::ERROR, ""));

    bool found = false;
    for (const auto& le : result.stack.layers) {
        if (le.name == "m1") {
            EXPECT_TRUE(approxEq(le.physical->z_start_nm, 60.0)); // m0:top = 50+10
            found = true;
        }
    }
    EXPECT_TRUE(found) << "m1 not found";
}

// ─── Test 20: Alignment reference to a layer in the downward pass ─────────────
// bm1 aligns its bottom to bm0:top; bm0 has explicit negative z (pre-resolved).
// Both bm1 and bm0 are in the downward region (below the anchor poly).

TEST(BuildStack, AlignmentInDownwardPass) {
    // File order: poly (anchor), bm0 (explicit z=-300), bm1 (aligned to bm0:top)
    std::vector<RawLayer> raw;

    raw.push_back(makeRaw(3, 0, 0.0, 22.0, "poly", "poly")); // anchor

    raw.push_back(makeRaw(2, 0, -300.0, 50.0, "metal", "bm0")); // explicit z=-300

    RawLayer bm1;
    bm1.layer_num = 1; bm1.datatype = 0; bm1.name = "bm1";
    bm1.align_bottom_to = "bm0:top"; // bm0:top = -300 + 50 = -250
    bm1.thickness_nm = 30.0; bm1.material = "metal";
    raw.push_back(bm1);

    auto result = buildStack("TEST", "1.0", raw);
    EXPECT_FALSE(hasDiag(result.diagnostics, GksDiagnostic::Level::ERROR, ""));

    bool found = false;
    for (const auto& le : result.stack.layers) {
        if (le.name == "bm1") {
            EXPECT_TRUE(approxEq(le.physical->z_start_nm, -250.0)); // bm0:top
            found = true;
        }
    }
    EXPECT_TRUE(found) << "bm1 not found";
}
