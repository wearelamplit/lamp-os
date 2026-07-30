#include "Compression.h"
#include "miniz.h"

namespace {
bool inflateInto(const uint8_t* d, size_t n, bool parseZlibHeader,
                 size_t maxOut, std::vector<uint8_t>& out) {
    tinfl_decompressor inflator;
    tinfl_init(&inflator);
    out.clear();
    size_t inOfs = 0;
    // Static dict avoids per-frame alloc failure under heap pressure (loop task only).
    static std::vector<uint8_t> dict(TINFL_LZ_DICT_SIZE);
    size_t dictOfs = 0;
    // No HAS_MORE_INPUT: the whole frame is present, so end-of-input is
    // end-of-stream. Setting it makes a stream that ends exactly at the buffer
    // end report NEEDS_MORE_INPUT and get dropped as if truncated.
    int flags = (parseZlibHeader ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0);
    for (;;) {
        size_t inBytes = n - inOfs;
        // outBytes stays TINFL_LZ_DICT_SIZE - dictOfs: tinfl requires the ring
        // window (dictOfs + outBytes) to be a power of two, so it can't be
        // clamped to the budget. The budget is enforced on the copy into out.
        size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
        tinfl_status st = tinfl_decompress(
            &inflator, d + inOfs, &inBytes,
            dict.data(), dict.data() + dictOfs, &outBytes, flags);
        inOfs += inBytes;
        // A compression bomb emits up to a full 32 KB window per call; copying
        // that into out would force a >maxOut contiguous alloc and bad_alloc on
        // the fragmented heap. Bail before growing out past maxOut.
        size_t room = maxOut - out.size();
        if (outBytes > room) {
            out.insert(out.end(), dict.data() + dictOfs,
                       dict.data() + dictOfs + room);
            return false;
        }
        out.insert(out.end(), dict.data() + dictOfs,
                   dict.data() + dictOfs + outBytes);
        dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) return true;
        if (st < TINFL_STATUS_DONE) return false;
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

bool maybeInflate(const uint8_t* d, size_t n, size_t maxOut, std::vector<uint8_t>& out) {
    if (isGzip(d, n)) {
        if (n <= 10) return false;
        return inflateInto(d + 10, n - 10, false, maxOut, out);
    }
    if (isLikelyZlib(d, n)) {
        if (inflateInto(d, n, true, maxOut, out)) return true;
        if (out.size() >= maxOut) return false;
    }
    if (inflateInto(d, n, false, maxOut, out)) return true;
    if (out.size() >= maxOut) return false;
    if (n > maxOut) return false;
    out.assign(d, d + n);
    return true;
}

}  // namespace Compression
