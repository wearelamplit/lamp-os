#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// Pure (host-portable) helper. Detects gzip/zlib/raw and inflates, mirroring
// the web client's pako behavior (try zlib, fall back to raw deflate). If the
// input is not recognized as compressed, it is copied through unchanged.
namespace Compression {
    // isGzip / isLikelyZlib are file-local in Compression.cpp; they're only
    // ever called from maybeInflate.
    bool maybeInflate(const uint8_t* d, size_t n, size_t maxOut, std::vector<uint8_t>& out);
}
