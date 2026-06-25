#include <unity.h>
#include "PaletteList.h"

// Real shapes from GET /api/v1/palettes/color/<id>.
static const char* SINGLE_HEX =
    R"({"paletteId":"211","name":"Blue and Yellow",)"
    R"("hexColors":["22455","22455","16766720"],"readOnly":true})";

static const char* SINGLE_RGB =
    R"({"paletteId":"b2d5","name":"Sith Legacy","colors":[)"
    R"({"r":0.721,"g":0.308,"b":0,"w":0,"am":0.2,"u":0},)"
    R"({"r":0.76,"g":0.15,"b":0.15,"w":0,"am":0.06,"u":0.16}],"readOnly":false})";

static const char* GROUP =
    R"({"paletteId":"218","name":"Solid and Rainbow","type":"GROUP",)"
    R"("children":[{"id":"300"},{"id":"302"}],"readOnly":true})";

void test_parses_hexcolors_from_strings(void) {
    Palette p;
    TEST_ASSERT_TRUE(parsePalette(SINGLE_HEX, p));
    TEST_ASSERT_EQUAL_STRING("211", p.id.c_str());
    TEST_ASSERT_EQUAL_STRING("Blue and Yellow", p.name.c_str());
    TEST_ASSERT_FALSE(p.isGroup);
    TEST_ASSERT_EQUAL(3, p.hexColors.size());
    TEST_ASSERT_EQUAL_UINT64(22455, p.hexColors[0]);     // 0x0057B7
    TEST_ASSERT_EQUAL_UINT64(16766720, p.hexColors[2]);  // 0xFFD800
}

void test_parses_rgb_channels(void) {
    Palette p;
    TEST_ASSERT_TRUE(parsePalette(SINGLE_RGB, p));
    TEST_ASSERT_FALSE(p.isGroup);
    TEST_ASSERT_EQUAL(2, p.colors.size());
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.721, p.colors[0].r);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.308, p.colors[0].g);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.2,   p.colors[0].am);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.16,  p.colors[1].u);
}

void test_parses_group_children(void) {
    Palette p;
    TEST_ASSERT_TRUE(parsePalette(GROUP, p));
    TEST_ASSERT_TRUE(p.isGroup);
    TEST_ASSERT_EQUAL(2, p.childIds.size());
    TEST_ASSERT_EQUAL_STRING("300", p.childIds[0].c_str());
    TEST_ASSERT_EQUAL(0, p.hexColors.size());
    TEST_ASSERT_EQUAL(0, p.colors.size());
}

void test_rejects_garbage(void) {
    Palette p;
    TEST_ASSERT_FALSE(parsePalette("not json", p));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_hexcolors_from_strings);
    RUN_TEST(test_parses_rgb_channels);
    RUN_TEST(test_parses_group_children);
    RUN_TEST(test_rejects_garbage);
    return UNITY_END();
}
