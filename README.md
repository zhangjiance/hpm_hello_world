# hpm_hello_world

A **sample application (APP)** for HPMicro RISC-V MCUs that works together with the [`hpm_dfu_boot`](../hpm_dfu_boot) DFU bootloader. It runs as the user firmware that the bootloader jumps to, and additionally exposes a **DFU runtime** interface so a host can ask it to reboot back into the bootloader for firmware upgrade.

> 中文说明见 [README_zh.md](./README_zh.md)。Build instructions: [README_build.md](./README_build.md) (English) / [README_build_zh.md](./README_build_zh.md) (中文).

---

## What This Project Does

This is a minimal "hello world" style application extended with DFU-runtime support:

- Prints `hello world` over the UART console and blinks an LED.
- Initializes the USB device (`src/usb_desc.c`) as a composite device consisting of:
  - a **DFU runtime** interface that responds to `DFU_DETACH` (i.e. `dfu-util -e`), then writes the DFU trigger magic to a retention register (BGPR / PDGO) and reboots into the bootloader;
  - a **CDC ACM VCOM** interface (virtual serial port) that echoes back any data it receives (loopback).
- Optionally, holding the user button for ~500 ms (5 consecutive reads) also triggers `hpm_dfu_reboot_to_dfu()`.

It is designed to be paired with the `hpm_dfu_boot` bootloader: the bootloader occupies the first 128K of Flash, and this APP is linked to run right after it.

### Memory Layout

| Item | Value |
| --- | --- |
| Bootloader region (reserved) | first 128K (`_dfu_bl_length=0x20000`) |
| APP load/start address | `0x80020000` |
| Linker script | HPM SDK `flash_dfu.ld` (`HPM_BUILD_TYPE=flash_dfu`) |

> The APP start address (`0x80020000`) matches the bootloader's `USBD_DFU_APP_DEFAULT_ADD`, so the bootloader can directly jump here after verifying the 4-byte DFU signature.

---

## Directory Structure

```
hpm_hello_world/
├── CMakeLists.txt          # Build script (APP: flash_dfu, reserves 128K for bootloader)
├── CMakePresets.json       # Release/Debug presets per board
├── boards/                 # Per-board configuration (BOARD_SEARCH_PATH)
├── src/
│   ├── hello_world.c       # Main: UART print, LED blink, key-to-bootloader
│   ├── hpm_dfu_trigger.c   # Trigger detection / reboot-to-DFU (shared with bootloader)
│   ├── hpm_dfu_trigger.h
│   ├── usb_desc.c          # USB composite device: DFU runtime (DETACH -> boot) + CDC ACM VCOM (loopback)
│   └── usb_config.h        # Local CherryUSB configuration
├── README.md               # Project overview (English, this file)
├── README_zh.md            # Project overview (中文)
├── README_build.md         # Build guide (English)
└── README_build_zh.md      # Build guide (中文)
```

---

## How to Build

See **[README_build.md](./README_build.md)** (English) / **[README_build_zh.md](./README_build_zh.md)** (中文) for details.

```bash
cmake --preset hpm5301evklite-release
cmake --build --preset hpm5301evklite-release
```

Artifacts are located at `build/<preset>/output/hello_world.elf` and `hello_world.hex`.

---

## Typical Usage (with hpm_dfu_boot)

1. Build and flash `hpm_dfu_boot` to the start of Flash (first 128K).
2. Build this APP and flash it at `0x80020000` (e.g. via DFU after entering bootloader mode).
3. On reset, the bootloader sees the valid APP signature and jumps to this application; the UART prints `hello world` and the LED blinks.
4. To upgrade firmware: from the host run `dfu-util -e` (or hold the user button ~500 ms) — the APP reboots into the bootloader, where you can download a new image over DFU.
