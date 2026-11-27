#include "util/Utf8.h"

#include <catch2/catch_test_macros.hpp>

using winssh::util::Utf8ToWide;
using winssh::util::WideToUtf8;

TEST_CASE("UTF-8 round trip", "[utf8]")
{
    const std::wstring wide = L"root@thiết-bị ✓";
    const std::string utf8 = WideToUtf8(wide);
    CHECK(utf8 == "root@thi\xE1\xBA\xBFt-b\xE1\xBB\x8B \xE2\x9C\x93");
    CHECK(Utf8ToWide(utf8) == wide);
}

TEST_CASE("UTF-8 empty input", "[utf8]")
{
    CHECK(WideToUtf8(L"").empty());
    CHECK(Utf8ToWide("").empty());
}

TEST_CASE("UTF-8 invalid bytes become replacement characters", "[utf8]")
{
    CHECK(Utf8ToWide("a\xFF" "b") == L"a�b");
}
