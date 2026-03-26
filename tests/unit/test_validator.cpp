#include "gtest/gtest.h"
#include "gks/core/LayerStack.hpp"
#include "gks/core/Validator.hpp"

using namespace gks;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static LayerEntry makeEntry(int layer_num, int datatype, const std::string& name = "layer",
                             std::optional<PhysicalProps> phys = std::nullopt) {
    LayerEntry e;
    e.layer_num = layer_num;
    e.datatype  = datatype;
    e.name      = name;
    e.purpose   = "drawing";
    e.physical  = phys;
    return e;
}

static PhysicalProps makePhys(double z_start, double thickness,
                               const std::string& material = "metal") {
    return PhysicalProps{z_start, thickness, material, std::nullopt};
}

static bool hasError(const std::vector<GksDiagnostic>& d, const std::string& substr) {
    for (const auto& x : d)
        if (x.level == GksDiagnostic::Level::ERROR && x.message.find(substr) != std::string::npos)
            return true;
    return false;
}

static bool hasWarn(const std::vector<GksDiagnostic>& d, const std::string& substr) {
    for (const auto& x : d)
        if (x.level == GksDiagnostic::Level::WARN && x.message.find(substr) != std::string::npos)
            return true;
    return false;
}

static bool hasInfo(const std::vector<GksDiagnostic>& d, const std::string& substr) {
    for (const auto& x : d)
        if (x.level == GksDiagnostic::Level::INFO && x.message.find(substr) != std::string::npos)
            return true;
    return false;
}

// ─── validate_identity: duplicate key ────────────────────────────────────────

TEST(ValidateIdentity, DuplicateKeyIsError) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "poly"));
    stack.layers.push_back(makeEntry(1, 0, "poly_dup")); // duplicate

    auto diags = validate_identity(stack);
    EXPECT_TRUE(hasError(diags, "duplicate"));
}

TEST(ValidateIdentity, NoDuplicateIsClean) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "poly"));
    stack.layers.push_back(makeEntry(1, 1, "poly_label")); // different datatype
    stack.layers.push_back(makeEntry(2, 0, "metal"));

    auto diags = validate_identity(stack);
    bool anyError = false;
    for (const auto& d : diags)
        if (d.level == GksDiagnostic::Level::ERROR) anyError = true;
    EXPECT_FALSE(anyError);
}

// ─── validate_identity: blank name ───────────────────────────────────────────

TEST(ValidateIdentity, BlankNameIsInfo) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "")); // blank name

    auto diags = validate_identity(stack);
    EXPECT_TRUE(hasInfo(diags, "empty name"));
}

TEST(ValidateIdentity, NonBlankNameNoInfo) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "m0"));

    auto diags = validate_identity(stack);
    EXPECT_FALSE(hasInfo(diags, "empty name"));
}

// ─── validate_for_lyp: dither_pattern range ──────────────────────────────────

TEST(ValidateForLyp, InvalidDitherPatternIsError) {
    LayerStack stack;
    auto e = makeEntry(1, 0, "m0");
    e.display.dither_pattern = -5; // invalid
    stack.layers.push_back(e);

    auto diags = validate_for_lyp(stack);
    EXPECT_TRUE(hasError(diags, "dither_pattern"));
}

TEST(ValidateForLyp, ValidDitherPatternNegOne) {
    LayerStack stack;
    auto e = makeEntry(1, 0, "m0");
    e.display.dither_pattern = -1; // valid (solid fill)
    stack.layers.push_back(e);

    auto diags = validate_for_lyp(stack);
    bool anyError = false;
    for (const auto& d : diags)
        if (d.level == GksDiagnostic::Level::ERROR) anyError = true;
    EXPECT_FALSE(anyError);
}

// ─── validate_for_lyp: line_style range ──────────────────────────────────────

TEST(ValidateForLyp, InvalidLineStyleIsError) {
    LayerStack stack;
    auto e = makeEntry(1, 0, "m0");
    e.display.line_style = -3; // invalid
    stack.layers.push_back(e);

    auto diags = validate_for_lyp(stack);
    EXPECT_TRUE(hasError(diags, "line_style"));
}

TEST(ValidateForLyp, ValidLineStyleNegOne) {
    LayerStack stack;
    auto e = makeEntry(1, 0, "m0");
    e.display.line_style = -1; // valid (no style)
    stack.layers.push_back(e);

    auto diags = validate_for_lyp(stack);
    bool anyError = false;
    for (const auto& d : diags)
        if (d.level == GksDiagnostic::Level::ERROR) anyError = true;
    EXPECT_FALSE(anyError);
}

// ─── validate_for_3d: missing PhysicalProps ──────────────────────────────────

TEST(ValidateFor3d, MissingPhysicalIsError) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "display_only")); // no physical

    auto diags = validate_for_3d(stack);
    EXPECT_TRUE(hasError(diags, "missing PhysicalProps"));
}

TEST(ValidateFor3d, AllPhysicalPresent) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "m0", makePhys(0.0, 36.0)));
    stack.layers.push_back(makeEntry(2, 0, "m1", makePhys(36.0, 36.0)));

    auto diags = validate_for_3d(stack);
    EXPECT_FALSE(hasError(diags, "missing"));
}

// ─── validate_for_3d: negative thickness ─────────────────────────────────────

TEST(ValidateFor3d, NegativeThicknessIsError) {
    LayerStack stack;
    stack.layers.push_back(makeEntry(1, 0, "bad", makePhys(0.0, -5.0)));

    auto diags = validate_for_3d(stack);
    EXPECT_TRUE(hasError(diags, "thickness_nm"));
    EXPECT_TRUE(hasError(diags, "negative"));
}

// ─── validate_for_3d: overlapping z with same material ───────────────────────

TEST(ValidateFor3d, OverlappingZSameMaterialIsWarn) {
    LayerStack stack;
    // Layer A: z=[0,100], material "metal"
    stack.layers.push_back(makeEntry(1, 0, "ma", makePhys(0.0, 100.0, "metal")));
    // Layer B: z=[50,150], material "metal" — overlaps A
    stack.layers.push_back(makeEntry(2, 0, "mb", makePhys(50.0, 100.0, "metal")));

    auto diags = validate_for_3d(stack);
    EXPECT_TRUE(hasWarn(diags, "overlap"));
}

TEST(ValidateFor3d, ParallelGroupNotFlaggedAsOverlap) {
    LayerStack stack;
    // Both at same z_start (parallel group) — NOT an overlap warning
    stack.layers.push_back(makeEntry(1, 0, "ma", makePhys(0.0, 50.0, "metal")));
    stack.layers.push_back(makeEntry(2, 0, "mb", makePhys(0.0, 50.0, "metal")));

    auto diags = validate_for_3d(stack);
    EXPECT_FALSE(hasWarn(diags, "overlap"));
}

TEST(ValidateFor3d, OverlappingZDifferentMaterialNoWarn) {
    LayerStack stack;
    // Same z range but different material — no warning
    stack.layers.push_back(makeEntry(1, 0, "ma", makePhys(0.0, 100.0, "metal")));
    stack.layers.push_back(makeEntry(2, 0, "mb", makePhys(50.0, 100.0, "dielectric")));

    auto diags = validate_for_3d(stack);
    EXPECT_FALSE(hasWarn(diags, "overlap"));
}

// ─── validate_full: combines all tiers ───────────────────────────────────────

TEST(ValidateFull, CombinesAllTiers) {
    LayerStack stack;
    // Duplicate key → validate_identity ERROR
    stack.layers.push_back(makeEntry(1, 0, "m0", makePhys(0.0, 36.0)));
    stack.layers.push_back(makeEntry(1, 0, "m0_dup", makePhys(36.0, 36.0)));
    // Invalid dither_pattern → validate_for_lyp ERROR
    stack.layers.back().display.dither_pattern = -9;

    auto diags = validate_full(stack);
    EXPECT_TRUE(hasError(diags, "duplicate"));
    EXPECT_TRUE(hasError(diags, "dither_pattern"));
}
