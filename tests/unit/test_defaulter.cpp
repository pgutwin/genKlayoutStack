#include "gtest/gtest.h"
#include "gks/core/LayerStack.hpp"
#include "gks/core/Defaulter.hpp"

using namespace gks;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static LayerStack makeStack(LayerStack::Defaults defs = {}) {
    LayerStack s;
    s.tech_name = "TEST";
    s.version   = "1.0";
    s.defaults  = defs;
    return s;
}

static LayerEntry makeDisplayEntry(int layer_num, int dt, const std::string& name) {
    LayerEntry e;
    e.layer_num = layer_num;
    e.datatype  = dt;
    e.name      = name;
    e.purpose   = "drawing";
    // physical absent (display only)
    return e;
}

static LayerEntry makePhysEntry(int layer_num, int dt, const std::string& name,
                                 const std::string& material = "") {
    LayerEntry e;
    e.layer_num = layer_num;
    e.datatype  = dt;
    e.name      = name;
    e.purpose   = "drawing";
    e.physical  = PhysicalProps{0.0, 36.0, material, std::nullopt};
    return e;
}

static bool hasInfo(const std::vector<GksDiagnostic>& d, const std::string& sub) {
    for (const auto& x : d)
        if (x.level == GksDiagnostic::Level::INFO && x.message.find(sub) != std::string::npos)
            return true;
    return false;
}

// ─── fill_alpha default applied ──────────────────────────────────────────────

TEST(ApplyDefaults, FillAlphaAppliedWhenAtStructDefault) {
    LayerStack::Defaults defs;
    defs.fill_alpha = 200;          // document-level default: 200

    auto stack = makeStack(defs);
    auto e = makeDisplayEntry(1, 0, "m0");
    // fill_alpha is 128 (struct default) → Defaulter should replace it
    EXPECT_EQ(e.display.fill_alpha, 128);
    stack.layers.push_back(e);

    auto diags = applyDefaults(stack);
    EXPECT_EQ(stack.layers[0].display.fill_alpha, 200);
    EXPECT_TRUE(hasInfo(diags, "fill_alpha"));
}

TEST(ApplyDefaults, FillAlphaNotOverriddenWhenExplicit) {
    LayerStack::Defaults defs;
    defs.fill_alpha = 200;

    auto stack = makeStack(defs);
    auto e = makeDisplayEntry(1, 0, "m0");
    e.display.fill_alpha = 64;      // explicit non-default value
    stack.layers.push_back(e);

    applyDefaults(stack);
    EXPECT_EQ(stack.layers[0].display.fill_alpha, 64); // unchanged
}

// ─── dither_pattern default applied ──────────────────────────────────────────

TEST(ApplyDefaults, DitherPatternAppliedWhenAtStructDefault) {
    LayerStack::Defaults defs;
    defs.dither_pattern = 9;        // document-level default

    auto stack = makeStack(defs);
    auto e = makeDisplayEntry(1, 0, "m0");
    // dither_pattern is -1 (struct default/solid) → should be replaced
    EXPECT_EQ(e.display.dither_pattern, -1);
    stack.layers.push_back(e);

    auto diags = applyDefaults(stack);
    EXPECT_EQ(stack.layers[0].display.dither_pattern, 9);
    EXPECT_TRUE(hasInfo(diags, "dither_pattern"));
}

TEST(ApplyDefaults, DitherPatternNotOverriddenWhenExplicit) {
    LayerStack::Defaults defs;
    defs.dither_pattern = 9;

    auto stack = makeStack(defs);
    auto e = makeDisplayEntry(1, 0, "m0");
    e.display.dither_pattern = 5;   // explicit non-default
    stack.layers.push_back(e);

    applyDefaults(stack);
    EXPECT_EQ(stack.layers[0].display.dither_pattern, 5);
}

// ─── line_style default applied ──────────────────────────────────────────────

TEST(ApplyDefaults, LineStyleAppliedWhenAtStructDefault) {
    LayerStack::Defaults defs;
    defs.line_style = 3;

    auto stack = makeStack(defs);
    auto e = makeDisplayEntry(1, 0, "m0");
    EXPECT_EQ(e.display.line_style, -1);
    stack.layers.push_back(e);

    auto diags = applyDefaults(stack);
    EXPECT_EQ(stack.layers[0].display.line_style, 3);
    EXPECT_TRUE(hasInfo(diags, "line_style"));
}

// ─── material default applied ─────────────────────────────────────────────────

TEST(ApplyDefaults, MaterialAppliedWhenEmpty) {
    LayerStack::Defaults defs;
    defs.material = "dielectric";

    auto stack = makeStack(defs);
    // Physical layer with no material (empty string)
    stack.layers.push_back(makePhysEntry(1, 0, "m0", ""));

    auto diags = applyDefaults(stack);
    ASSERT_TRUE(stack.layers[0].physical.has_value());
    EXPECT_EQ(stack.layers[0].physical->material, "dielectric");
    EXPECT_TRUE(hasInfo(diags, "material"));
}

TEST(ApplyDefaults, MaterialNotOverriddenWhenExplicit) {
    LayerStack::Defaults defs;
    defs.material = "dielectric";

    auto stack = makeStack(defs);
    stack.layers.push_back(makePhysEntry(1, 0, "m0", "metal")); // explicit

    applyDefaults(stack);
    EXPECT_EQ(stack.layers[0].physical->material, "metal");
}

// ─── multiple layers, mixed explicit and default ──────────────────────────────

TEST(ApplyDefaults, MixedLayersOnlyDefaultUnsetOnes) {
    LayerStack::Defaults defs;
    defs.dither_pattern = 9;

    auto stack = makeStack(defs);

    auto e0 = makeDisplayEntry(1, 0, "with_explicit");
    e0.display.dither_pattern = 5;   // explicit
    stack.layers.push_back(e0);

    auto e1 = makeDisplayEntry(2, 0, "with_default");
    // dither_pattern == -1 (struct default) — will be replaced
    stack.layers.push_back(e1);

    applyDefaults(stack);

    EXPECT_EQ(stack.layers[0].display.dither_pattern, 5);  // unchanged
    EXPECT_EQ(stack.layers[1].display.dither_pattern, 9);  // defaulted
}

// ─── no defaults set → no changes ────────────────────────────────────────────

TEST(ApplyDefaults, NoDefaultsSetNoChanges) {
    auto stack = makeStack(); // empty defaults
    auto e = makeDisplayEntry(1, 0, "m0");
    e.display.fill_alpha     = 128;
    e.display.dither_pattern = -1;
    stack.layers.push_back(e);

    auto diags = applyDefaults(stack);
    EXPECT_TRUE(diags.empty());
    EXPECT_EQ(stack.layers[0].display.fill_alpha, 128);
    EXPECT_EQ(stack.layers[0].display.dither_pattern, -1);
}

// ─── display-only layers don't get material default ───────────────────────────

TEST(ApplyDefaults, MaterialNotAppliedToDisplayOnlyLayer) {
    LayerStack::Defaults defs;
    defs.material = "dielectric";

    auto stack = makeStack(defs);
    stack.layers.push_back(makeDisplayEntry(1, 0, "display_only")); // no physical

    auto diags = applyDefaults(stack);
    EXPECT_FALSE(hasInfo(diags, "material"));
    EXPECT_FALSE(stack.layers[0].physical.has_value());
}
