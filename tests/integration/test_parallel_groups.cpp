#include "gtest/gtest.h"
#include "gks/io/ScriptWriter.hpp"
#include "gks/io/ScriptReader.hpp"
#include "gks/core/LayerStack.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Build a stack with a parallel group: (4,0) and (5,0) at same z_start
gks::LayerStack makeParallelStack() {
    gks::LayerStack s;
    s.tech_name = "TEST";
    s.version   = "0.1";

    // epi_contact: has layer_expression referencing both layers
    {
        gks::LayerEntry e;
        e.layer_num = 4;
        e.datatype  = 0;
        e.name      = "epi_contact";
        e.display.fill_color  = {0xff, 0x88, 0x00};
        e.display.frame_color = {0xcc, 0x66, 0x00};
        gks::PhysicalProps p;
        p.z_start_nm       = 105.0;
        p.thickness_nm     = 45.0;
        p.material         = "tungsten";
        p.layer_expression = std::string("input(4,0) + input(5,0)");
        e.physical = p;
        s.layers.push_back(e);
    }

    // gate_contact: consumed layer, no layer_expression
    {
        gks::LayerEntry e;
        e.layer_num = 5;
        e.datatype  = 0;
        e.name      = "gate_contact";
        e.display.fill_color  = {0xaa, 0x44, 0x00};
        e.display.frame_color = {0x88, 0x22, 0x00};
        gks::PhysicalProps p;
        p.z_start_nm   = 105.0;
        p.thickness_nm = 45.0;
        p.material     = "tungsten";
        e.physical = p;
        s.layers.push_back(e);
    }

    return s;
}

} // anonymous namespace

// Test 1: zz() block is emitted; no standalone z(input(4, 0) outside of it
TEST(ParallelGroups, ParallelGroupZzEmitted) {
    auto stack = makeParallelStack();
    std::string path = "/tmp/gks_pg_emit.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);

    // Must contain a zz( block
    EXPECT_NE(content.find("zz("), std::string::npos);

    // Count standalone z(input(4, 0) — it should NOT appear as a top-level z() call
    // The only occurrence of z(input(4, 0) should be inside the zz block
    // Verify there is no top-level "z(input(4, 0)" (i.e., not preceded by spaces)
    // The inner z calls inside zz have leading spaces: "  z(input(...)"
    // A top-level z call would start at column 0 with "z(input("
    size_t pos = 0;
    int standalone_count = 0;
    while ((pos = content.find("\nz(input(4, 0)", pos)) != std::string::npos) {
        ++standalone_count;
        ++pos;
    }
    // Also check for start of file
    if (content.substr(0, 14) == "z(input(4, 0)") {
        ++standalone_count;
    }
    EXPECT_EQ(standalone_count, 0) << "Layer (4,0) should not appear as a standalone z() call";

    std::filesystem::remove(path);
}

// Test 2: layer_expression survives write → read round-trip
TEST(ParallelGroups, LayerExpressionRoundtrip) {
    auto stack = makeParallelStack();
    std::string path = "/tmp/gks_pg_roundtrip.rb";
    auto wr = gks::writeScript(stack, path);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rr = gks::readScript(path);
    ASSERT_TRUE(rr.has_value()) << rr.error().message;
    ASSERT_EQ(rr->layers.size(), 2u);

    // First layer should have layer_expression
    ASSERT_TRUE(rr->layers[0].layer_expression.has_value());
    EXPECT_EQ(*rr->layers[0].layer_expression, "input(4,0) + input(5,0)");

    std::filesystem::remove(path);
}

// Test 3: Both layers in the zz block are present with correct z_start
TEST(ParallelGroups, ZzBlockBothLayersParsed) {
    auto stack = makeParallelStack();
    std::string path = "/tmp/gks_pg_both.rb";
    auto wr = gks::writeScript(stack, path);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto rr = gks::readScript(path);
    ASSERT_TRUE(rr.has_value()) << rr.error().message;
    ASSERT_EQ(rr->layers.size(), 2u);

    // Both layers should have z_start_nm = 105.0
    for (const auto& rl : rr->layers) {
        SCOPED_TRACE("layer_num=" + std::to_string(rl.layer_num.value_or(-1)));
        ASSERT_TRUE(rl.z_start_nm.has_value());
        EXPECT_NEAR(*rl.z_start_nm, 105.0, 1e-6);
    }

    std::filesystem::remove(path);
}
