#include "console/OutputWriter.h"

#include "util/Utf8.h"

#include <windows.h>

namespace winssh::console
{
namespace
{
void WriteStdout(std::string_view bytes)
{
    HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    while (!bytes.empty())
    {
        DWORD written = 0;
        if (!::WriteFile(out, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written == 0)
            return;
        bytes.remove_prefix(written);
    }
}
} // namespace

OutputWriter::OutputWriter() : sink_(WriteStdout) {}

OutputWriter::OutputWriter(Sink sink) : sink_(std::move(sink)) {}

void OutputWriter::Write(std::string_view bytes)
{
    pending_.append(bytes);
    const size_t n = util::CompleteUtf8Prefix(pending_);
    if (n == 0)
        return;
    sink_(std::string_view(pending_).substr(0, n));
    pending_.erase(0, n);
}

void OutputWriter::Flush()
{
    if (!pending_.empty())
    {
        sink_(pending_);
        pending_.clear();
    }
}

} // namespace winssh::console
