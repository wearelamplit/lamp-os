# Build a Lamp Conversion Kit

As part of the lamp assembly, you'll need to make a few items ahead of assembly. We refer to this collection of parts as a lamp kit. This guide is technical and needs a few specialized tools and supplies

## Prerequisites

![Main Lamp Components](images/important-lamp-parts.jpg)

### Parts List

- USB Type A connector
- SPT-1 18ga lamp cord - we encourage reuse of existing lamp cords where possible. Simply cut the AC plug off the end of existing cords
- An ESP32 board compatible with Lamp OS and our [pinout](https://lastminuteengineers.com/esp32-pinout-reference/)
  - 30 pin ESP32 DEVKIT-V1 style
  - Dual core/4MB Flash/520k SRAM/Wifi + BT
  - Unsoldered/Unwelded pins. Clipping pins off is no fun!
- 5V Neopixel style LED strip. It is critical to use 4 channel RGB + Warm White LEDs for this project to avoid incompatibility:
  - RGBWW (SK6812 chipset)
  - White PCB
  - 60 LEDs/m
  - Silicone Jacketed - IP67
- 24ga stranded hookup wire in blue, red, black and yellow
- [3D printed bulb and optional diffuser](../3dprint/bulb/)
- heat shrink tubing: 10mm diameter for the USB connector
- 4" white/clear zip ties
- 1/8 IPS lamp nuts
- Wire nuts - Grey 22ga
- Wire nuts - Blue 18ga
- Large size freezer bags

Looking to buy parts? check out our [BOM and sourcing sheet](https://docs.google.com/spreadsheets/d/13LMiKESsMFRVg8935Jd-dmCTtnCKnP0fI2C8PypP58M/edit?usp=sharing)

### Tools

- Wire cutter
- Wire stripper
- Wire crimper
- Heat gun
- Scissors
- Soldering iron with temperature control - chisel tip - 750F for leaded solder
- Solder - eutectic rosin core - Tin 63% / Lead 37% / 0.025"
- Silicone sealant - neutral cure type
- Gorilla brand Crystal Clear tape
- 3D printer
- [3D printed tools](../3dprint/tools/)

### Using silicone sealant

Note that sealant may take up to a day to cure, so prepare a spot that will be protected from movement and lined with cardboard so the sealant can't accidentally get on clothes or furniture

## Wiring up the Board

Cut 4 24ga wires to 6" length:

- Red
- Black
- Yellow
- Blue

Strip the wires to 2mm and tin the board and wiring. Connect the wires in a surface mounted fashion as follows

- Red - Vin
- Black - GND
- Yellow - D12
- Blue - D14

Collect the wires and zip tie them to the side of the board

![Board wiring](images/board-1.jpg)

Make sure to program the board at the [board programmer utility](https://update.lamplit.ca) before you continue

## Preparing the Base LEDs

Cut 3 24ga wires to 36" length:

- Black
- Blue
- Red

Strip the wires to 2-3mm and tin the board and wiring. Connect the wires in a surface mounted fashion as follows:

- Black - GND
- Blue - Din
- Red - 5V

Cut 30 LEDs from the reel along the line on the pads

![Base LED wiring](images/base-led-1.jpg)

In order to protect the wiring, silicone the LED strip squarely inside the jacketing

![Silicone the base LED strip](images/base-led-2.jpg)

The finished LED strip should be fully covered by the jacket

![Ensure the strip is completely surrounded by the isulator](images/base-led-3.jpg)

Roll up the LEDs to protect them until assembly

![Ensure the strip is completely surrounded by the isulator](images/base-led-4.jpg)

## Preparing the Bulb

Cut 3 24ga wires to 8" length:

- Black
- Yellow
- Red

Strip the wires to 2-3mm and tin the board and wiring. Connect the wires in a surface mounted fashion as follows:

- Black - GND
- Yellow - Din
- Red - 5V

Cut exactly 30 LEDs from the strip reel along the line on the pads

![Bulb wiring](images/shade-led-1.jpg)

Feed the wires into the bulb's bottom window

![Inserting the LED strip into the bulb](images/shade-led-2.jpg)

Wrap the LEDs around the bulb and tuck the end of the jacketing into the top window

![Wrapping the LED strip](images/shade-led-3.jpg)

Use clear tape to secure and protect the silicone jacketing from dust

![Securing the strip with tape](images/shade-led-4.jpg)

Cut the tape slightly to get good nozzle placement and inject silicone into the bottom and top window to completely seal the LED strip from weather

![Silicone the LED strip ends](images/shade-led-5.jpg)

Tuck in the wires to complete the bulb assembly

![Tidying the bulb wiring](images/shade-led-6.jpg)

## Preparing the USB Lamp Cord

Cut around 7 feet of lamp cord or use a recycled cord from an older lamp. Strip around 4mm from the ends of the lamp cord and tin them.

![Strip and tin the lamp wire](images/lamp-wire-1.jpg)

Tin the two edges of the connector. You can use a clip to add a little weight for stabilization while soldering. Keep the amount of tinning light

![Tin the connector pads](images/lamp-wire-2.jpg)

Bend the wires a little so they're parallel to the pads and heat the tinned surface until they sink into the connector and bond. Note that the ribbed or striped side of the wire (traditionally AC Neutral) will connect to USB GND

![Bond the wires](images/lamp-wire-3.jpg)

With your thumb over the wire, pinch the lamp cord so the two windows are visible

![Pinch the jacketing to ensure the shell snaps in place](images/lamp-wire-4.jpg)

Clamp the top shell to the wire and ensure the clips fit into the windows

![Clip on the shell](images/lamp-wire-5.jpg)

Using the blue crimper setting (18-16ga), fully crimp the connector

![Crimp the connector](images/lamp-wire-6.jpg)

Cut about 2" of heat shrink tubing and slide it over the connector

![Prep the heatshrink](images/lamp-wire-7.jpg)

Aim for slightly above the top shell and use the heat gun to shrink it to fit

![Position and heat the tubing](images/lamp-wire-8.jpg)

The completed cord should look something like this. Tidy it into a loop for bagging.

![Completed lamp cord](images/lamp-wire-9.jpg)

## Completing a kit

With all of the soldering done, you can now assemble the rest of the kit

![Kit contents illustration](images/kit-content-illustration.jpg)

- 1x assembled bulb
- 1x assembled board
- 1x assembled USB lamp cord
- 1x assembled base LED strip
- 6x 4" white zip ties
- 2x grey wire nuts
- 2x blue wire nuts
- 1x 1/8 IPS lamp nut

If you're preparing for a workshop, put all of these items into a freezer bag for later assembly

![Kit contents bagged](images/lamp-kit-complete.jpg)

That's the end of this tutorial. If you're looking to build immediately, check out our simplified [Assembly Guide](files/lamp-build-instructions.pdf)
