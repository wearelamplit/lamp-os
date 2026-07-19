import asyncio
import base64
from aiohttp import web, WSMsgType
from bleak import BleakScanner, BleakClient
from mac import stable_mac
from errors import wire_error

LAMP_MFG_ID = 0xA455

CONNS = {}       # deviceId(colon-MAC) -> Conn
ADDR_BY_ID = {}  # deviceId -> bleak address discovered during scan


class Conn:
    def __init__(self, address):
        self.address = address
        self.client = None
        self._loop = None
        self.lock = asyncio.Lock()  # serialize GATT ops (page cursor is per-conn state)
        self.last_notify = {}       # charUuid -> bytes
        self.notify_queues = {}     # charUuid -> asyncio.Queue
        self.watch_queues = []      # asyncio.Queue per /watch WS

    def on_drop(self):
        # bleak calls this from its own thread; use the captured loop.
        for q in self.watch_queues:
            self._loop.call_soon_threadsafe(q.put_nowait, False)


async def scan_ws(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    def cb(device, adv):
        mfg = adv.manufacturer_data.get(LAMP_MFG_ID)
        if mfg is None:
            return
        dev_id = stable_mac(device)
        ADDR_BY_ID[dev_id] = device.address
        asyncio.create_task(ws.send_json({
            "manufacturerData": {str(LAMP_MFG_ID): list(mfg)},
            "advName": adv.local_name or "",
            "platformName": device.name or "",
            "remoteId": dev_id,
            "rssi": adv.rssi,
        }))

    async with BleakScanner(cb):
        async for msg in ws:  # hold open until client disconnects
            if msg.type == WSMsgType.ERROR:
                break

    return ws


async def connect(request):
    dev_id = request.match_info["id"]
    addr = ADDR_BY_ID.get(dev_id, dev_id)
    conn = CONNS.get(dev_id) or Conn(addr)
    try:
        conn._loop = asyncio.get_running_loop()
        conn.client = BleakClient(addr, disconnected_callback=lambda _c: conn.on_drop())
        await conn.client.connect()
        # >=247 matches firmware kPageMaxChunkSize=244; CoreBluetooth/WinRT negotiate automatically, BlueZ needs an explicit request.
        print(f"[bridge] connected {dev_id} mtu={conn.client.mtu_size}")
        CONNS[dev_id] = conn
        return web.json_response({"ok": True})
    except Exception as e:
        CONNS.pop(dev_id, None)
        return web.json_response({"error": "TIMEOUT", "message": str(e)}, status=504)


def _status_for(code):
    return 504 if code == "TIMEOUT" else 500


async def read_char(request):
    dev_id = request.match_info["id"]
    chr_uuid = request.match_info["chr"]
    conn = CONNS.get(dev_id)
    if not conn or not conn.client:
        return web.json_response({"error": "DISCONNECTED"}, status=500)
    try:
        async with conn.lock:  # page cursor is per-conn state; serialize all GATT ops
            data = await conn.client.read_gatt_char(chr_uuid)
        return web.json_response({"data": base64.b64encode(bytes(data)).decode()})
    except Exception as e:
        code = wire_error(e)
        return web.json_response({"error": code, "message": str(e)}, status=_status_for(code))


async def write_char(request):
    dev_id = request.match_info["id"]
    chr_uuid = request.match_info["chr"]
    body = await request.json()
    data = base64.b64decode(body["data"])
    response = not body.get("withoutResponse", False)
    conn = CONNS.get(dev_id)
    if not conn or not conn.client:
        return web.json_response({"error": "DISCONNECTED"}, status=500)
    try:
        async with conn.lock:
            await conn.client.write_gatt_char(chr_uuid, data, response=response)
        return web.json_response({"ok": True})
    except Exception as e:
        code = wire_error(e)
        return web.json_response({"error": code, "message": str(e)}, status=_status_for(code))


async def subscribe(request):
    dev_id = request.match_info["id"]
    chr_uuid = request.match_info["chr"]
    conn = CONNS.get(dev_id)
    if not conn or not conn.client:
        return web.json_response({"error": "DISCONNECTED"}, status=500)
    def on_notify(_char, data):
        conn.last_notify[chr_uuid] = bytes(data)
        q = conn.notify_queues.get(chr_uuid)
        if q:
            conn._loop.call_soon_threadsafe(q.put_nowait, bytes(data))
    async with conn.lock:
        await conn.client.start_notify(chr_uuid, on_notify)
    return web.json_response({"ok": True})


async def notify_ws(request):
    dev_id = request.match_info["id"]
    chr_uuid = request.match_info["chr"]
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    conn = CONNS.get(dev_id)
    if conn is None:
        await ws.close()
        return ws
    q = asyncio.Queue()
    conn.notify_queues[chr_uuid] = q
    # Replay the last value so a notify that fired before the WS attached is not lost.
    if chr_uuid in conn.last_notify:
        await ws.send_json({"data": base64.b64encode(conn.last_notify[chr_uuid]).decode()})
    while not ws.closed:
        data = await q.get()
        await ws.send_json({"data": base64.b64encode(data).decode()})
    return ws


async def watch_ws(request):
    dev_id = request.match_info["id"]
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    conn = CONNS.get(dev_id)
    if not conn:
        await ws.send_json({"connected": False})
        await ws.close()
        return ws
    q = asyncio.Queue()
    conn.watch_queues.append(q)
    # Seed current connected state on attach.
    is_connected = conn.client is not None and conn.client.is_connected
    await ws.send_json({"connected": is_connected})
    try:
        while not ws.closed:
            edge = await q.get()
            await ws.send_json({"connected": edge})
    finally:
        conn.watch_queues.remove(q)
    return ws


async def disconnect(request):
    dev_id = request.match_info["id"]
    conn = CONNS.get(dev_id)
    if conn and conn.client:
        try:
            await conn.client.disconnect()
        except Exception:
            pass
    return web.json_response({"ok": True})


def build_app():
    app = web.Application()
    app.add_routes([
        web.get("/scan", scan_ws),
        web.post("/connect/{id}", connect),
        web.post("/disconnect/{id}", disconnect),
        web.get("/read/{id}/{svc}/{chr}", read_char),
        web.post("/write/{id}/{svc}/{chr}", write_char),
        web.post("/subscribe/{id}/{svc}/{chr}", subscribe),
        web.get("/notify/{id}/{svc}/{chr}", notify_ws),
        web.get("/watch/{id}", watch_ws),
    ])
    return app


if __name__ == "__main__":
    web.run_app(build_app(), host="0.0.0.0", port=8080)
