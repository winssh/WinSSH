#include "util/Base64.h"

namespace winssh::util
{

std::string Base64NoPad(const unsigned char *data, size_t len)
{
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3)
    {
        const unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        out += kTable[(v >> 6) & 63];
        out += kTable[v & 63];
    }
    if (i < len)
    {
        unsigned v = data[i] << 16;
        if (i + 1 < len)
            v |= data[i + 1] << 8;
        out += kTable[(v >> 18) & 63];
        out += kTable[(v >> 12) & 63];
        if (i + 1 < len)
            out += kTable[(v >> 6) & 63];
    }
    return out;
}

} // namespace winssh::util
