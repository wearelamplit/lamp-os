#include "Compression.h"
#include "miniz.h"

namespace {
bool inflateInto(const uint8_t* d, size_t n, bool parseZlibHeader,
                 std::vector<uint8_t>& out) {
    tinfl_decompressor inflator;
    tinfl_init(&inflator);
    out.clear();
    size_t inOfs = 0;
    std::vector<uint8_t> dict(TINFL_LZ_DICT_SIZE);
    size_t dictOfs = 0;
    int flags = (parseZlibHeader ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0);
    for (;;) {
        size_t inBytes = n - inOfs;
        size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
        tinfl_status st = tinfl_decompress(
            &inflator, d + inOfs, &inBytes,
            dict.data(), dict.data() + dictOfs, &outBytes,
            flags | TINFL_FLAG_HAS_MORE_INPUT);
        inOfs += inBytes;
        out.insert(out.end(), dict.data() + dictOfs,
                   dict.data() + dictOfs + outBytes);
        dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) return true;
        if (st < TINFL_STATUS_DONE) return false;
        if (inOfs >= n && st == TINFL_STATUS_NEEDS_MORE_INPUT) return false;
    }
}
}  // namespace

namespace Compression {

namespace {
bool isGzip(const uint8_t* d, size_t n) {
    return n >= 2 && d[0] == 0x1f && d[1] == 0x8b;
}

bool isLikelyZlib(const uint8_t* d, size_t n) {
    if (n < 2) return false;
    uint8_t cmf = d[0], flg = d[1];
    if ((cmf & 0x0f) != 8) return false;
    if ((cmf >> 4) > 7) return false;
    if (((cmf << 8) + flg) % 31 != 0) return false;
    return true;
}
}  // namespace

bool maybeInflate(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    if (isGzip(d, n)) {
        if (n <= 10) return false;
        return inflateInto(d + 10, n - 10, false, out);
    }
    if (isLikelyZlib(d, n)) {
        if (inflateInto(d, n, true, out)) return true;
    }
    if (inflateInto(d, n, false, out)) return true;
    out.assign(d, d + n);
    return true;
}

}  // namespace Compression
