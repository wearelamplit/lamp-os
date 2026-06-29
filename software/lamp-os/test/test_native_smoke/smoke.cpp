#include <unity.h>

void setUp() {}
void tearDown() {}

void test_smoke() {
    TEST_ASSERT_TRUE(true);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
