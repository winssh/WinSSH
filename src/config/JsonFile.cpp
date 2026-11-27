#include "config/JsonFile.h"

#include "util/Utf8.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace winssh::config
{

std::optional<std::string> ReadTextFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.rfind("\xEF\xBB\xBF", 0) == 0)
        text.erase(0, 3);
    return text;
}

bool WriteTextFileAtomic(const std::filesystem::path &path, const std::string &content, std::string *error)
{
    auto fail = [&](const std::string &msg) {
        if (error)
            *error = msg;
        return false;
    };
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tmp = path;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return fail("cannot write " + util::WideToUtf8(tmp.wstring()));
        out << content;
        out.flush();
        if (!out)
            return fail("cannot write " + util::WideToUtf8(tmp.wstring()));
    }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD e = ::GetLastError();
        std::filesystem::remove(tmp, ec);
        return fail("cannot replace " + util::WideToUtf8(path.wstring()) + " (error " + std::to_string(e) + ")");
    }
    return true;
}

} // namespace winssh::config
