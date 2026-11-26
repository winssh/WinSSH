#pragma once

#include <cstddef>
#include <string>

namespace winssh::util
{

// Standard base64 without '=' padding (the format OpenSSH uses for SHA256 fingerprints).
std::string Base64NoPad(const unsigned char *data, size_t len);

} // namespace winssh::util
