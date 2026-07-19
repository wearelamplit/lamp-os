def wire_error(exc):
    # type: (Exception) -> str
    m = str(exc).lower()
    if "encrypt" in m:
        return "ENCRYPTION_REQUIRED"
    if "not discovered" in m or "discover" in m:
        return "DISCOVERY_FAILED"
    if "disconnect" in m or "not connected" in m:
        return "DISCONNECTED"
    if "timeout" in m or "timed out" in m:
        return "TIMEOUT"
    if "not found" in m or "no characteristic" in m:
        return "NOT_FOUND"
    return "UNKNOWN"
