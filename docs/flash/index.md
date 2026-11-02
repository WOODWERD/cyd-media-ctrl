# Flash it

Media Ctrl **x0.3.0** is a public beta. The quickest way to put it on a board: plug it in, open this page in **Chrome or Edge**, press the button and pick the port. Safari and Firefox can't, because they lack Web Serial.

<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
<esp-web-install-button manifest="manifest.json">
  <span slot="unsupported">This browser can't talk to serial ports — use Chrome or Edge.</span>
  <span slot="not-allowed">Serial access is blocked here (needs HTTPS).</span>
</esp-web-install-button>

!!! note "Which board"
    The prebuilt image is for the **USB-C / 2-USB** Cheap Yellow Display (ST7789 panel). The original micro-USB board needs the `cyd` build; see [Firmware & code](../firmware.md).

If the port doesn't appear, hold **BOOT** on the board while plugging it in, then try again. Some boards need a USB-C → USB-A cable, not C-to-C.

## From a terminal instead

```
pip install esptool
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 460800 write_flash 0x0 media-ctrl-x0.3.0-cyd2usb.bin
```

That full image starts with a clean pairing table, so any device paired before has to forget Media Ctrl and pair again. To update a board that's already paired, without re-pairing, write only the app at 0x10000:

```
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 460800 write_flash 0x10000 media-ctrl-x0.3.0-cyd2usb-app-0x10000.bin
```

Both images are in the repo under `release/` and attached to the [x0.3.0 release](https://github.com/WOODWERD/cyd-media-ctrl/releases/tag/x0.3.0).

## Then

Pair it: see [Getting started](../GETTING_STARTED.md).
