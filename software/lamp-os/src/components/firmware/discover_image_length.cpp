#include "firmware_signature.hpp"

#include <cstring>

namespace lamp { namespace firmware {

bool discoverSignedImageLength(FirmwareByteReader reader, size_t partitionLen,
                               uint32_t* outLen) {
  if (!reader || !outLen || partitionLen < kLsigFooterLen) return false;

  // 256-byte sliding window with a (kLsigMagicLen - 1) overlap so a magic
  // straddling a window boundary is not missed. Scans forward and returns
  // the FIRST (lowest-offset) valid footer: a lamp re-flashed with a smaller
  // image over esptool leaves the prior larger image's footer in the
  // un-erased partition tail; the forward scan finds the running image's
  // footer first because no stale footer survives below it.
  constexpr size_t kWindow = 256;
  size_t scanStart = 0;
  uint8_t buf[kWindow + kLsigMagicLen - 1];

  while (scanStart < partitionLen) {
    const size_t take = (partitionLen - scanStart > kWindow)
                            ? kWindow
                            : (partitionLen - scanStart);
    const size_t extra = (scanStart + take + kLsigMagicLen - 1 <= partitionLen)
                             ? (kLsigMagicLen - 1)
                             : 0;
    const int got = reader(scanStart, take + extra, buf);
    if (got != static_cast<int>(take + extra)) return false;

    for (size_t off = 0; off < take; ++off) {
      if (off + kLsigMagicLen > take + extra) break;
      if (buf[off]     == 'L' && buf[off + 1] == 'S' &&
          buf[off + 2] == 'I' && buf[off + 3] == 'G') {
        const uint32_t footerStart = static_cast<uint32_t>(scanStart + off);
        uint8_t lenBytes[4];
        const int lr = reader(footerStart + kLsigSignedLenOffset, 4, lenBytes);
        if (lr != 4) return false;
        const uint32_t signedRegionLen =
            static_cast<uint32_t>(lenBytes[0])
            | (static_cast<uint32_t>(lenBytes[1]) << 8)
            | (static_cast<uint32_t>(lenBytes[2]) << 16)
            | (static_cast<uint32_t>(lenBytes[3]) << 24);
        if (signedRegionLen == footerStart) {
          *outLen = footerStart + static_cast<uint32_t>(kLsigFooterLen);
          return true;
        }
      }
    }
    scanStart += take;
  }
  return false;
}

}}  // namespace lamp::firmware
