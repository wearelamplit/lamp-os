# Wisp

A small magical light that signals to others. Wisp is dedicated infrastructure (Seeed Xiao ESP32-C6 with a remote antenna) — the rebuilt identity of the old `software/artnet-repeater/` board. It is not a lamp; it is a non-participating helper that sits at the edge of the mesh.

## Two jobs

1. **Palette bridge.** Follows a palette from Straylight over OSC 1.0 (SLIP frames on the USB serial console, or UDP port 9000 on the softAP, its STA LAN join or wired Ethernet) and fans out per-lamp paired colors to the mesh over ESP-NOW. The app picks which Straylight palette role it follows (primary, secondary, tertiary or fill); the address is `/straylight/palette/<role>/colours`, flat RGB floats 0..1.
2. **ArtNet bridge.** Paints pre-mesh lamps, which have no mesh stack and take color only as ArtNet DMX over WiFi. This is the reason wisp runs a softAP and a BLE stage beacon at all — see below.

## Painting pre-mesh lamps

Pre-mesh lamp firmware joins a WiFi network whose credentials it finds in a non-connectable BLE advert (magic `42007`, payload `<ssid\0><password\0>`), then listens for ArtNet DMX on UDP/6454. So wisp brings up a softAP on the mesh channel in `WIFI_AP_STA` (`main.cpp`), advertises those credentials via `StageBeacon`, and emits universe-1 frames from `src/artnet/`.

One frame carries 8 fixtures of 10 channels (shade RGBW, base RGBW, mode, parameter). A pre-mesh lamp picks its fixture slot at random on boot, so assignment is neither stable nor unique — unlike the mesh path, which keys each lamp's colors on its real MAC. Wisp fills the 8 slots by running synthetic MACs through the same `TupleSampler` the mesh path uses, so both fleets sample one palette. Base and shade are swapped into the frame to match `PaintDistributor`'s surface mapping; without that a mixed room renders the two fleets inverted.

Emission is ~1 Hz (a faster stream would steal TX airtime from ESP-NOW RX on the shared radio) and unicasts to each joined station in AP mode, because WiFi broadcast has no MAC-layer ack. Turning paint off just stops the frames: pre-mesh lamps hit their 30 s ArtNet timeout and fade back to their own scene.

## Off-by-default paint

Status reporting and mesh-inventory tracking run unconditionally at boot. The OSC UDP listener is bound, and the WiFi STA join to a stored network runs, only while the source mode is Osc (the STA join also only while Ethernet is not up); each attempt scans the mesh channel only, joins only if the network is there, and backs off (10 s doubling to 5 min) when it isn't, so the radio stays on the mesh. **Choosing Manual or Straylight in the app is the opt-in to paint, and the choice is stored.** A fresh wisp starts Off and paints nothing until someone picks a source. After a reboot or power blip the stored source comes back and painting resumes: Manual repaints its stored palette, Straylight resumes as soon as a palette arrives. Off comes back as Off.

In Osc mode a stream gap is not an opt-out: after 30 s with nothing from Straylight (or as soon as Straylight sends a blank one for an unbound role) the wisp lets lamps ease home and shows its off color, but stays in Osc mode, and the next palette resumes painting with no app action.

Prefer USB serial or Ethernet for the OSC feed. Over WiFi UDP the wisp hears Straylight's whole feed, not just its palette, on the radio it shares with the mesh, and a restate burst can overflow lwIP's 6-datagram socket queue and drop the palette, possibly on every restate, so a change lost that way waits for the next change.

Lamp overrides are transient (RAM-only on the lamp side). When wisp drops or paint mode goes off, lamps re-render their saved base config. The user's stored config is never touched.

## Wired Ethernet

A WIZnet W5500 SPI module gives the wisp a cable to the venue network, so the OSC feed arrives without the radio: the one radio stays on channel 6 doing ESP-NOW and the softAP for pre-mesh lamps, and the STA never joins. The module is optional and every wisp runs the same firmware; there is no setting.

| W5500 | XIAO pin | GPIO |
|---|---|---|
| SCLK | D8 | 19 |
| MISO | D9 | 20 |
| MOSI | D10 | 18 |
| SCS (CS) | D3 | 21 |
| INT | D2 | 2 |
| RST | not wired; the module's own pull-up holds it, and the driver resets the chip over SPI | |
| 3V3 / GND | 3V3 / GND | |

The pins are the `WISP_ETH_*` build flags in `platformio.ini`. They avoid everything the wisp and the board already use: D1 (GPIO 1) drives the LED ring, D0 (GPIO 0) is kept free, D6/D7 (GPIO 16/17) are UART0 where the ROM prints its boot log, GPIO 3 and 14 are the RF switch and antenna select, GPIO 12/13 are USB, and none of the C6 strapping pins (GPIO 4, 5, 8, 9, 15) is used. D4/D5 (GPIO 22/23, the board's I2C pair) stay free. Use a 3.3 V module.

At boot the wisp probes for the W5500 once (`ETH.begin`, DHCP). With no module fitted the probe fails within about 200 ms, the SPI bus is freed and the five pins are reset (`gpio_reset_pin`: GPIO function, output and input off, weak pull-up on), the console prints `[eth] no W5500 (cs=21); Ethernet off`, and nothing retries; the wisp behaves as one without the code. The GPIO ISR service the probe installed stays installed, which costs nothing while no pin uses it. The probe does drive SCLK, MOSI and CS for that moment, which is why those header pins are reserved for the module.

A module-less boot also prints the framework's own error lines just before `[eth] no W5500`, and they are expected:

- `E (..) w5500.mac: w5500_reset(..): reset timeout` then `E (..) w5500.mac: emac_w5500_init(..): reset w5500 failed`, or, when the floating MISO line reads as a cleared reset bit, `E (..) w5500.mac: W5500 version mismatched, expected 0x04, got 0x..` then `E (..) w5500.mac: emac_w5500_init(..): verify chip ID failed`
- `E (..) esp_eth: esp_eth_driver_install(..): init mac failed`
- `[..][E][ETH.cpp:..] beginSPI(): SPI Ethernet driver install failed: ..`

With a module fitted, Ethernet counts as up when it has a link and a DHCP lease. While it is up the STA join is not wanted even in Osc, and an association already held drops; when the cable or lease goes, a wisp in Osc with stored credentials starts the channel-6 join again at once. The OSC socket listens on every interface, so Straylight (or anything else sending OSC) points at the Ethernet address on UDP 9000. The address shows in the app's WiFi row (`ethIp` in `wispStatus`) and on the console with `eth:show`. The DHCP hostname is the softAP SSID made hostname-safe (`wisp-<name>`, lowercase, invalid characters as hyphens), set at boot, so a rename shows in the router's lease table after the next reboot.

IPv4 multicast is blocked at the chip, so multicast sACN on a lighting LAN costs the wisp nothing. Broadcast is not filtered: every broadcast frame (ARP, Art-Net broadcast and ArtPoll, DHCP, discovery chatter) costs an SPI read and a heap buffer of a full frame in the W5500 receive task, which runs above the wisp's loop on the C6's one core. Put the wisp on a segment that does not carry broadcast Art-Net.

The softAP for pre-mesh lamps is 192.168.4.0/24 (the core's default). A venue LAN on that same subnet collides with it. Art-Net to softAP lamps is pinned to the softAP netif, so painting pre-mesh lamps is unaffected; the risk is IP traffic toward the venue side, such as replies to the OSC sender, which the wisp may route out the softAP instead of the cable. Give the venue network any other range.

IP forwarding is on in the prebuilt framework (`CONFIG_LWIP_IP_FORWARD=y`), so the wisp plainly routes between the softAP and Ethernet, with no NAT. A softAP lamp's packet addressed off its subnet goes out the cable, and replies cannot find their way back unless the venue router has a route to 192.168.4.0/24. This is the same exposure the STA join already had. Turning it off takes a `custom_sdkconfig` entry, at the cost of a full IDF rebuild; the wisp does not do that.

## How the user talks to it

Wisp takes no BLE connections: its only BLE is the non-connectable stage beacon that hands pre-mesh lamps the softAP credentials. The app talks to wisp via any nearby lamp acting as a proxy: the lamp surfaces wisp's last-overheard `MSG_WISP_HELLO` on a BLE characteristic, and forwards ops back to wisp over the mesh. WiFi credentials are configured the same way — out of the box, flash wisp once over USB and configure it from the app.

## Wire format

The messages the wisp sends (`MSG_WISP_HELLO`, `MSG_WISP_CLAIM`, `MSG_WISP_STATE`, `MSG_WISP_PALETTE`, `MSG_OVERRIDE_BRIGHTNESS`, `MSG_CONTROL_OP` wisp envelopes) are documented in [`docs/dev/networking.md`](../../docs/dev/networking.md). The wisp takes no part in the lamps' firmware OTA; it is flashed over USB only. LSIG signed-firmware footer format lives alongside the build scripts at [`scripts/README.md`](../../scripts/README.md).
