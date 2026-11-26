#include "console/InputReader.h"
#include "console/OutputWriter.h"
#include "ssh/KnownHosts.h"
#include "util/Base64.h"
#include "util/Utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using winssh::util::CompleteUtf8Prefix;

TEST_CASE("CompleteUtf8Prefix holds back an incomplete character", "[utf8]")
{
    const std::string check = "\xE2\x9C\x93"; // U+2713, 3 bytes
    CHECK(CompleteUtf8Prefix("") == 0);
    CHECK(CompleteUtf8Prefix("abc") == 3);
    CHECK(CompleteUtf8Prefix("ab" + check) == 5);
    CHECK(CompleteUtf8Prefix("ab" + check.substr(0, 1)) == 2);
    CHECK(CompleteUtf8Prefix("ab" + check.substr(0, 2)) == 2);
    CHECK(CompleteUtf8Prefix("\xF0\x9F\x98") == 0);       // 4-byte emoji, 3 bytes so far
    CHECK(CompleteUtf8Prefix("\xF0\x9F\x98\x80") == 4);   // complete emoji
    CHECK(CompleteUtf8Prefix("a\x80\x80") == 3);           // stray continuation bytes are not held
}

TEST_CASE("OutputWriter never splits a character across writes", "[console]")
{
    std::vector<std::string> writes;
    winssh::console::OutputWriter w([&](std::string_view s) { writes.emplace_back(s); });
    w.Write("ok \xE2\x9C");
    w.Write("\x93 done");
    w.Flush();
    REQUIRE(writes.size() == 2);
    CHECK(writes[0] == "ok ");
    CHECK(writes[1] == "\xE2\x9C\x93 done");
}

TEST_CASE("OutputWriter flushes a held tail at the end", "[console]")
{
    std::string all;
    winssh::console::OutputWriter w([&](std::string_view s) { all += s; });
    w.Write("x\xE2");
    CHECK(all == "x");
    w.Flush();
    CHECK(all == "x\xE2");
}

TEST_CASE("Utf16Joiner joins surrogate pairs", "[console]")
{
    winssh::console::Utf16Joiner j;
    std::string out;
    j.Push(L'a', out);
    j.Push(static_cast<wchar_t>(0xD83D), out); // high surrogate of U+1F600
    CHECK(out == "a");
    j.Push(static_cast<wchar_t>(0xDE00), out);
    CHECK(out == "a\xF0\x9F\x98\x80");
    j.Push(static_cast<wchar_t>(0xDE00), out); // lone low surrogate is dropped
    CHECK(out == "a\xF0\x9F\x98\x80");
}

TEST_CASE("Base64NoPad matches OpenSSH fingerprint encoding", "[util]")
{
    const unsigned char a[] = {'f'};
    const unsigned char b[] = {'f', 'o'};
    const unsigned char c[] = {'f', 'o', 'o'};
    CHECK(winssh::util::Base64NoPad(a, 1) == "Zg");
    CHECK(winssh::util::Base64NoPad(b, 2) == "Zm8");
    CHECK(winssh::util::Base64NoPad(c, 3) == "Zm9v");
}

TEST_CASE("KnownHostsName uses [host]:port for non-default ports", "[ssh]")
{
    CHECK(winssh::ssh::KnownHostsName("10.0.0.108", 22) == "10.0.0.108");
    CHECK(winssh::ssh::KnownHostsName("10.0.0.108", 2222) == "[10.0.0.108]:2222");
}
