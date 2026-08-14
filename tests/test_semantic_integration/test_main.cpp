// PlatformIO googletest entry point (native host tests, issue #51 section 8).
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
