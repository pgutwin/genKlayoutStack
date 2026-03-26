#include "gtest/gtest.h"
#include "gks/io/ScriptWriter.hpp"
#include "gks/core/LayerStack.hpp"

#include <fstream>
#include <string>
#include <filesystem>

namespace {

// Helper: read an entire file into a string
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Helper: build a minimal LayerStack with the given layers
gks::LayerStack makeStack(const std::vector<gks::LayerEntry>& layers) {
    gks::LayerStack s;
    s.tech_name = "TEST";
    s.version   = "0.1";
    s.layers    = layers;
    return s;
}

gks::LayerEntry makeLayer(int ln, int dt, const std::string& name,
                          double z_start, double thickness,
                          gks::Color fill, gks::Color frame,
                          std::optional<std::string> layer_expr = std::nullopt) {
    gks::LayerEntry e;
    e.layer_num = ln;
    e.datatype  = dt;
    e.name      = name;
    e.display.fill_color  = fill;
    e.display.frame_color = frame;
    gks::PhysicalProps p;
    p.z_start_nm    = z_start;
    p.thickness_nm  = thickness;
    p.material      = "metal";
    p.layer_expression = layer_expr;
    e.physical = p;
    return e;
}

} // anonymous namespace

// Test 1: Standard layer emits correctly
TEST(ScriptWriter, StandardLayer) {
    gks::Color fill  = {0x00, 0x55, 0xff};
    gks::Color frame = {0x00, 0x33, 0xcc};
    auto stack = makeStack({ makeLayer(6, 0, "m0", 150.0, 36.0, fill, frame) });

    std::string path = "/tmp/gks_sw_standard.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);
    EXPECT_NE(content.find("z(input(6, 0)"),   std::string::npos);
    EXPECT_NE(content.find("zstart: 150.0.nm"), std::string::npos);
    EXPECT_NE(content.find("height: 36.0.nm"),  std::string::npos);
    EXPECT_NE(content.find("fill: 0x0055ff"),   std::string::npos);
    EXPECT_NE(content.find("name: \"m0\""),     std::string::npos);

    std::filesystem::remove(path);
}

// Test 2: Negative z emits correctly
TEST(ScriptWriter, NegativeZ) {
    gks::Color fill  = {0x88, 0x88, 0x88};
    gks::Color frame = {0x44, 0x44, 0x44};
    auto stack = makeStack({ makeLayer(0, 0, "substrate", -300.0, 300.0, fill, frame) });

    std::string path = "/tmp/gks_sw_negz.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);
    EXPECT_NE(content.find("zstart: -300.0.nm"), std::string::npos);

    std::filesystem::remove(path);
}

// Test 3: zz() block emits correctly for a parallel group
TEST(ScriptWriter, ZzBlockEmit) {
    gks::Color fill4  = {0xff, 0x88, 0x00};
    gks::Color frame4 = {0xcc, 0x66, 0x00};
    gks::Color fill5  = {0xaa, 0x44, 0x00};
    gks::Color frame5 = {0x88, 0x22, 0x00};

    // Layer (4,0) has layer_expression referencing both (4,0) and (5,0)
    auto epi  = makeLayer(4, 0, "epi_contact", 105.0, 45.0, fill4, frame4,
                          std::string("input(4,0) + input(5,0)"));
    // Layer (5,0) is consumed — no layer_expression
    auto gate = makeLayer(5, 0, "gate_contact", 105.0, 45.0, fill5, frame5);

    auto stack = makeStack({ epi, gate });

    std::string path = "/tmp/gks_sw_zz.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);

    // Should have a zz( block
    EXPECT_NE(content.find("zz("), std::string::npos);
    EXPECT_NE(content.find("name: \"epi_contact\""), std::string::npos);
    // Inner z() calls should be present
    EXPECT_NE(content.find("z(input(4, 0)"), std::string::npos);
    EXPECT_NE(content.find("z(input(5, 0)"), std::string::npos);
    // end keyword
    EXPECT_NE(content.find("end"), std::string::npos);

    // zz( should appear exactly once (not duplicated)
    size_t pos = 0;
    int count = 0;
    while ((pos = content.find("zz(", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    EXPECT_EQ(count, 1);

    std::filesystem::remove(path);
}

// Test 4: Display-only layers are skipped (not emitted as z() calls)
TEST(ScriptWriter, SkipsDisplayOnly) {
    gks::LayerEntry e;
    e.layer_num = 9;
    e.datatype  = 1;
    e.name      = "label";
    e.display.fill_color  = {0xff, 0xff, 0xff};
    e.display.frame_color = {0xcc, 0xcc, 0xcc};
    // No physical props

    auto stack = makeStack({ e });

    std::string path = "/tmp/gks_sw_skip.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);

    // No z(input( should appear
    EXPECT_EQ(content.find("z(input("), std::string::npos);
    // But the layer name should appear in the header comment
    EXPECT_NE(content.find("label"), std::string::npos);

    std::filesystem::remove(path);
}

// Test 5: Layers are sorted by z_start_nm ascending in output
TEST(ScriptWriter, SortedByZ) {
    gks::Color c = {0x80, 0x80, 0x80};
    auto la = makeLayer(1, 0, "top",    100.0, 36.0, c, c);
    auto lb = makeLayer(2, 0, "middle",  50.0, 36.0, c, c);
    auto lc = makeLayer(3, 0, "bottom",   0.0, 36.0, c, c);

    // Add in reverse z order to the stack IR
    auto stack = makeStack({ la, lb, lc });

    std::string path = "/tmp/gks_sw_sort.rb";
    auto r = gks::writeScript(stack, path);
    ASSERT_TRUE(r.has_value()) << r.error().message;

    std::string content = readFile(path);

    // Find positions of the three layers
    size_t pos_bottom = content.find("\"bottom\"");
    size_t pos_middle = content.find("\"middle\"");
    size_t pos_top    = content.find("\"top\"");

    ASSERT_NE(pos_bottom, std::string::npos);
    ASSERT_NE(pos_middle, std::string::npos);
    ASSERT_NE(pos_top,    std::string::npos);

    // bottom (z=0) should appear before middle (z=50), which before top (z=100)
    EXPECT_LT(pos_bottom, pos_middle);
    EXPECT_LT(pos_middle, pos_top);

    std::filesystem::remove(path);
}
