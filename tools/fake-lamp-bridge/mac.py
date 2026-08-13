import hashlib


def stable_mac(device):
    # type: (object) -> str
    """Return a stable colon-MAC deviceId for a bleak device.

    On Linux/Windows bleak gives a real MAC; pass it through upper-cased.
    On macOS bleak gives a CoreBluetooth UUID — derive a deterministic
    locally-administered MAC from it so the app's MAC-keyed paths work
    identically on every host OS.
    """
    addr = device.address
    if len(addr) == 17 and addr.count(":") == 5:
        return addr.upper()
    h = hashlib.sha1(addr.encode()).digest()
    b = bytearray(h[:6])
    b[0] = (b[0] & 0xFC) | 0x02  # locally-administered, unicast
    return ":".join("{:02X}".format(x) for x in b)
