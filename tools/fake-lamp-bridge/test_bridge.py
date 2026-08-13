from mac import stable_mac
from errors import wire_error


class FakeDev:
    def __init__(self, address, name):
        self.address = address
        self.name = name


def test_stable_mac_from_linux_style_address():
    d = FakeDev("aa:bb:cc:dd:ee:ff", "Flora")
    assert stable_mac(d) == "AA:BB:CC:DD:EE:FF"


def test_stable_mac_from_macos_uuid_is_deterministic():
    d = FakeDev("6F1B2C3D-0000-1111-2222-333344445555", "Flora")
    m1 = stable_mac(d)
    m2 = stable_mac(d)
    assert m1 == m2                 # stable across calls
    assert len(m1.split(":")) == 6  # MAC-shaped
    assert m1[1] in "26AE"         # locally-administered nibble set


def test_wire_error_maps_disconnect_and_encryption():
    assert wire_error(Exception("Device disconnected")) == "DISCONNECTED"
    assert wire_error(Exception("Insufficient encryption")) == "ENCRYPTION_REQUIRED"
    assert wire_error(Exception("services not discovered")) == "DISCOVERY_FAILED"
    assert wire_error(Exception("timed out")) == "TIMEOUT"
    assert wire_error(Exception("weird")) == "UNKNOWN"


def test_wire_error_not_found():
    assert wire_error(Exception("not found")) == "NOT_FOUND"
    assert wire_error(Exception("no characteristic")) == "NOT_FOUND"


if __name__ == "__main__":
    test_stable_mac_from_linux_style_address()
    print("PASS test_stable_mac_from_linux_style_address")
    test_stable_mac_from_macos_uuid_is_deterministic()
    print("PASS test_stable_mac_from_macos_uuid_is_deterministic")
    test_wire_error_maps_disconnect_and_encryption()
    print("PASS test_wire_error_maps_disconnect_and_encryption")
    test_wire_error_not_found()
    print("PASS test_wire_error_not_found")
    print("All tests passed.")
