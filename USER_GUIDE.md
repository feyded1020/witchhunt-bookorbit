# Witch Reader User Guide

Welcome to **Witch Reader** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [Witch Reader User Guide](#witch-reader-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Recent Books Screen](#34-recent-books-screen)
    - [3.5 Book Info Screen](#35-book-info-screen)
    - [3.6 File Transfer Screen](#36-file-transfer-screen)
      - [3.6.1 Calibre Wireless Transfers](#361-calibre-wireless-transfers)
      - [3.6.2 USB Transfer (over the cable)](#362-usb-transfer-over-the-cable)
      - [3.6.3 USB Drive (X4 Pro and LilyGo T5 S3)](#363-usb-drive-x4-pro-and-lilygo-t5-s3)
    - [3.7 Settings](#37-settings)
      - [3.7.1 Display](#371-display)
      - [3.7.2 Reader](#372-reader)
      - [3.7.3 Controls](#373-controls)
      - [3.7.4 System](#374-system)
      - [3.7.5 OPDS Servers (Multiple Libraries)](#375-opds-servers-multiple-libraries)
      - [3.7.6 Web Settings (WiFi + OPDS)](#376-web-settings-wifi--opds)
      - [3.7.7 KOReader Sync Quick Setup](#377-koreader-sync-quick-setup)
    - [3.8 Sleep Screen](#38-sleep-screen)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Touch Controls](#5-touch-controls)
    - [5.1 The two master switches](#51-the-two-master-switches)
    - [5.2 Menus, lists and everything outside a book](#52-menus-lists-and-everything-outside-a-book)
    - [5.3 The reading page: taps and holds](#53-the-reading-page-taps-and-holds)
    - [5.4 The reading page: swipes](#54-the-reading-page-swipes)
    - [5.5 Corners, and two fingers](#55-corners-and-two-fingers)
    - [5.6 Reading light](#56-reading-light)
    - [5.7 Per-device differences](#57-per-device-differences)
    - [5.8 Turning it off](#58-turning-it-off)
    - [5.9 If a gesture does nothing](#59-if-a-gesture-does-nothing)
  - [6. Chapter Selection Screen](#6-chapter-selection-screen)
  - [7. Current Limitations \& Roadmap](#7-current-limitations--roadmap)
  - [8. Troubleshooting Issues \& Escaping Bootloop](#8-troubleshooting-issues--escaping-bootloop)


## 1. Hardware Overview

Four devices are supported: the **Xteink X3** and **Xteink X4** (buttons only), and the
**Xteink X4 Pro** and **LilyGo T5 S3 Pro** (buttons and a touchscreen).

### Button Layout

On the **X3 and X4**, the firmware uses the manufacturer's own button layout by default:

| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Volume Up**, **Volume Down**, **Reset** |

The **X4 Pro** and **LilyGo T5 S3** have far fewer keys, and make up the difference with the
touchscreen:

| Device | Physical keys | Back / Confirm |
| --- | --- | --- |
| **X4 Pro** | Up, Down, Power | Capacitive Home key — **tap** for Confirm, **hold** for Back |
| **LilyGo T5 S3** | Down, Power | Capacitive Home key — **tap** for Confirm, **hold** for Back |

The T5 S3 has no **Up** key at all, so paging a list backward is done by tapping the scroll
bar or swiping — see **[Touch Controls](#5-touch-controls)**.

Button layout can be customized in the **[Controls Settings](#373-controls)**.

On the touch devices everything can also be driven by finger — see
**[Touch Controls](#5-touch-controls)**. Touch never replaces a button: every key keeps its
function, and all touch behaviour can be switched off.

### Taking a Screenshot
When the Power Button and Volume Down button are pressed at the same time, it will take a screenshot and save it in the folder `screenshots/`.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.
In the **[Controls Settings](#373-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE]
> On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. It shows the most recently read book as a cover thumbnail and provides navigation to **[Reading Mode](#4-reading-mode)**, the **[Browse Files](#33-browse-files-screen)** screen, the **[Recent Books](#34-recent-books-screen)** screen, the **[File Transfer](#36-file-transfer-screen)** screen, and **[Settings](#37-settings)**. A weather panel and clock are also accessible from the Home screen when configured.

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen is a full-featured file and folder browser.

* **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up and down through folders and books. Long-pressing these buttons scrolls a full page at a time.
* **Open Selection:** Press **Confirm** to open a folder or read a selected book.
* **Context Menu:** Hold and release **Confirm** to open a context menu for the selected item. Actions include: open, mark as read, view book info, set as sleep screen, flash a `.bin` firmware file, and delete.

#### Sorting

Files and folders can be sorted by **name**, **date**, **size**, or **type**, in either ascending or descending order. The sort order is configurable from the context menu or a dedicated sort button in the browser toolbar.

#### Large folders

Folders with many entries are handled via an SD-card-backed index so memory use stays bounded regardless of folder size.

### 3.4 Recent Books Screen

The Recent Books screen shows recently opened books as a **cover grid**, displaying cover art, title, and author. Selecting a book opens it at the last read position.

### 3.5 Book Info Screen

The Book Info screen shows full metadata for a book: cover image, title, author, description (paged if long), and reading statistics. It is accessible from the context menu in Browse Files or from the reader menu while reading.

### 3.6 File Transfer Screen

The File Transfer screen allows you to upload new e-books to the device. When you enter the screen, you'll be prompted with a WiFi selection dialog and then your X4 will start hosting a web server.

See the [webserver docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [webserver docs](./docs/webserver.md) for details.

### 3.6.1 Calibre Wireless Transfers

Witch Reader supports sending books from Calibre using the CrossPoint Reader device plugin.

1. Install the plugin in Calibre:
   - Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.
   - Download the zip file.
   - Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.
2. On the device: File Transfer → Connect to Calibre → Join a network.
3. Make sure your computer is on the same WiFi network.
4. In Calibre, click "Send to device" to transfer books.

### 3.6.2 USB Transfer (over the cable)

If you'd rather not use WiFi, you can transfer books over the USB cable. On the device choose **File Transfer → USB Transfer**; the screen then waits for a host connection and shows per-operation status (e.g. *Receiving 'book.epub'*, *Saved …*).

This uses a serial protocol that is wire-compatible with **[MicroReader](https://github.com/CidVonHighwind/microreader)** by CidVonHighwind — a clean-room, independently written reimplementation of its protocol — so MicroReader's host tools work unchanged:

- Its **Calibre device plugin** ("Send to device" over USB), and
- Its `tools/serial_cmd.py` command-line tool (`--upload`, `--list`, …).

A **Total Commander / Double Commander file-system plugin (WFX)** that works with both Witch Reader and MicroReader is available at **[jpirnay/x4-filemanager-plugin](https://github.com/jpirnay/x4-filemanager-plugin)**. It lets you browse and manage the SD card directly from the file manager over the USB cable.

Full credit to CidVonHighwind for the original protocol and tooling. While a transfer is in progress the device pauses on-screen redraws and on-wire logging so the binary stream stays clean.

**Open this screen first, then connect.** Opening the USB port from a computer briefly resets the device (a hardware quirk of the ESP32-C3's USB port). As long as you've opened the **USB Transfer** screen first, the device automatically returns to this screen after that reset, so the transfer just works — press **Back** when you're done. (If you connect while on another screen, the device will simply reboot to Home; open USB Transfer and reconnect.)

### 3.6.3 USB Drive (X4 Pro and LilyGo T5 S3)

On the **Xteink X4 Pro** and the **LilyGo T5 S3**, *File Transfer* offers **USB Drive** in place of USB Transfer. The reader appears on your computer as an ordinary USB stick, so you can copy books on and off — and reorganise folders, or clear caches — with your normal file manager. No plugin, no protocol, no WiFi.

Choose **File Transfer → USB Drive**, then connect the cable. The screen says *USB Drive Connected* once your computer has it mounted; give it up to half a minute the first time.

**When you're done, eject the drive on your computer** (or just unplug the cable). The reader then restarts by itself and returns to Home — that restart is deliberate, because your computer may have changed anything on the card and the reader has to re-read it. Nothing is lost: your reading position is saved before the drive starts.

While the drive is connected the reader is a disk and nothing else — buttons, sleep and the page you were reading are all suspended until you eject.

> These two boards can do this because their USB-C port is wired straight to the processor's own USB hardware. The X3 and X4 cannot: their ESP32-C3 has no USB device controller for it.

### 3.7 Settings

The Settings screen allows you to configure the device's behavior.

#### 3.7.1 Display

- **Reading light** *(devices with a frontlight or backlight only)*: On/off, brightness, warmth (two-channel devices) and whether waking restores the light. See **[Reading light](#56-reading-light)**.
- **Screen Edge Margin**: How much clearance to leave between the text and the edge of the glass — "Narrow" (default, the existing behaviour), "Medium" (+5 px) or "Large" (+10 px). Useful where the case comes close to the live pixels, as on the LilyGo T5 S3. Pages re-flow when it changes.
- **Time to Sleep**: Slider from 0 (Never) to 60 minutes; sets the inactivity period before the device sleeps.
- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  - "Dark" (default) - The Witch Reader logo on a dark background
  - "Light" - The same logo on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#38-sleep-screen) for more information
  - "Cover" - The cover of the currently open book
  - "None" - A blank screen
  - "Cover + Custom" - Book cover with fallback to Custom behavior
  - "Page Overlay" - A transparent PNG composited over the current reader page (book content shows through)
  - "Quick Resume" - A minimal screen that resumes reading immediately on wake
- **Sleep Screen Cover Mode**: How to display the cover image:
  - "Fit" (default) - Scale to fit, white borders
  - "Crop" - Scale and crop to fill the screen
- **Sleep Screen Cover Filter**: Filter applied to the cover image:
  - "None" (default) - Grayscale
  - "Contrast" - Black & white without grayscale conversion
  - "Inverted" - Inverted black & white
  - "Adaptive" - Stretches the picture between its own darkest and lightest points
  - "Equalize" - Spreads the tones by how much of the picture carries them; stronger than Adaptive, and the better choice for a mostly dark cover with a small bright title
- **Sleep Screen Overlay**: Tint overlay applied on top of the sleep image (useful for dimming a cover or overlay image):
  - "Off" (default), "White", "Gray", "Black"
- **Sleep Image Pick Mode**: How to cycle through images in the Custom sleep screen:
  - "Random" (default) - Pick a random image each time
  - "Sequential" - Cycle through images in order
- **Quick Resume Timeout**: Whether the Quick Resume sleep screen auto-clears on next wake.
- **Hide Battery %**: Where to suppress the battery percentage in the status bar:
  - "Never" (default), "In Reader", "Always"
- **Refresh Frequency** (submenu): Settings for screen refresh behaviour while reading:
  - **Refresh Frequency** - Slider (0 = Never, up to 60) for how often a full refresh runs to clear ghosting
  - **Refresh After Image Pages** - Whether to do an extra refresh after pages containing images
- **Sunlight Fading Fix**: Software fix for white X4 models that may fade in direct sunlight. "OFF" (default) / "ON".
- **UI Theme**: Visual theme for the device UI:
  - "Classic" - The original theme
  - "Lyra" - Rounded elements and menu icons
  - "Lyra Extended" - Lyra with 3 books on the Home Screen
  - "Lyra Carousel" - Lyra with a full cover carousel on the Home Screen

#### 3.7.2 Reader

- **Reading Orientation**: Screen orientation for reading:
  - "Portrait" (default), "Landscape CW", "Inverted", "Landscape CCW"

**EPUB Font** (submenu):
- **Font Family**: Font used for EPUB reading. Includes built-in fonts (Bookerly, Noto Sans) plus any fonts installed on the SD card.
- **Font Size**: "Tiny", "Small", "Medium" (default), "Large", "X Large"
- **Text Anti-Aliasing**: Smooth grey edges on text. Slows page turns slightly. "ON" / "OFF"
- **Fast AA** *(X3 only)*: Swaps the slow 53-frame grayscale waveform for a fast 7-frame LUT (~130 ms). Mid-tones appear slightly darker. "ON" / "OFF"
- **Text Darkness**: Ink density for rendered text: "Normal" (default), "Dark", "Extra Dark", "Max Dark"

**TXT/Markdown Font** (submenu):
- **Font Family**: Font used when reading `.txt` and `.md` files (independent of the EPUB font).
- **Font Size**: "Tiny", "Small", "Medium" (default), "Large", "X Large"

**Layout** (submenu):
- **Paragraph Alignment**: "Justified" (default), "Left", "Center", "Right", "Book Style"

**Spacing** (submenu):
- **Screen Margin**: Left/right margin in Reading Mode, 5–40 px in 5 px steps.
- **Line Spacing**: "Tight", "Normal" (default), "Wide"
- **Extra Paragraph Spacing**: "ON" adds vertical space between paragraphs; "OFF" uses first-line indentation instead.

**Images** (submenu):
- **Images**: "Display" (default), "Placeholder" (show a box where the image would be), "Suppress" (skip images entirely)
- **Large Image Placeholder**: Whether to substitute an explicit placeholder for images that are too large to display inline. "ON" / "OFF"

- **Embedded Style**: Use the EPUB's own HTML/CSS styling. "ON" (default) / "OFF"
- **Hyphenation**: Automatic hyphenation while reading. "ON" / "OFF"
- **Bionic Reading**: Bold the first half of each word to guide the eye. "ON" / "OFF"
- **Guide Dots**: Draw a small dot centered in the space between words to guide the eye along the line (idea borrowed from [CrossInk](https://github.com/uxjulia/CrossInk)). "ON" / "OFF"
- **Synthetic TOC Fallback**: Generate a table of contents from headings when the EPUB has an invalid or missing TOC. "ON" / "OFF"
- **Customise Status Bar**: Opens a submenu to configure every element of the reading status bar individually: upper and lower progress bars (Book / Chapter / Hidden, with thickness), status item position (Top / Bottom), chapter page count, book progress percentage, title display (Book / Chapter / Hidden), battery, and clock.

#### 3.7.3 Controls

- **Remap Front Buttons**: Reassign the physical function of each bottom-edge button.
- **Button Actions** (submenus — one per logical button: Back, Confirm, Left, Right, Up/Page Back, Down/Page Forward, Power): For each button, independently configure the **Short Press**, **Double Press**, and **Long Press** action. Available actions include: page forward/back, skip 10 pages, go home, sleep, force refresh, force fast refresh, open TOC, open bookmarks, star page, footnotes, next/previous chapter, exit reader, open reader menu, toggle bionic reading, KOReader sync, cycle font size, larger/smaller text, cycle orientation (either direction), quick overrides, toggle touch navigation, ignore, and — on devices with a light — toggle reading light, light brighter and light dimmer — plus light warmer and light cooler on a device with a warm/cool light.
- **Button Actions Overview**: A read-only overview screen showing the current short/double/long press mapping for every button at a glance.
- **Touch Navigation** *(touch devices only)*: Whether touch does anything outside the reader — list rows, covers, the home menu, the settings tabs, the on-screen button hints. "ON" / "OFF". See **[Touch Controls](#5-touch-controls)**.
- **Touch Page Turn** *(touch devices only)*: How the reading page turns pages by touch — "Off", "Tap Zones", "Swipe", "Tap Zones (Inverted)". It governs page turns only; the reader menu and the reading light stay reachable either way.
- **Tap Action** *(touch devices only)*: Whether tapping a list row selects it — "Select, then activate" (default) — or opens it straight away, "Activate immediately".
- **Gesture actions** *(touch devices only)*: One entry per gesture — the sideways swipes, the four edge swipes (left and right edge columns, up and down), the two ten-page zone swipes, the five tap zones, the same five as long taps, the four corner holds, pinch and rotation — each assigned an action from the same list the buttons offer. See **[Touch Controls](#5-touch-controls)**.
- **Gesture overview** *(touch devices only)*: A read-only screen drawing the tap, hold and swipe zones with your current assignments in them. Generated from live settings, so it is the authority if the documentation disagrees.
- **Tilt Page Turn** *(X3 only)*: Use the tilt sensor to turn pages by tilting the device. Sub-settings:
  - **Enable Tilt Page Turn**: "ON" / "OFF"
  - **Tilt Right action**: "None", "Next Page", "Prev Page"
  - **Tilt Left action**: "None", "Next Page", "Prev Page"

#### 3.7.4 System

- **Language**: Set the system language (see **[Supported Languages](#supported-languages)**).
- **Show Hidden Files**: Show files and folders whose names start with `.`. "ON" / "OFF"
- **Show File Extensions**: Show file extensions in the file browser. "ON" / "OFF"

**Network**:
- **WiFi Networks**: Add, remove, and connect to WiFi networks.
- **KOReader Sync**: Configure and authenticate KOReader progress sync. See [KOReader Sync Quick Setup](#377-koreader-sync-quick-setup).
- **OPDS Servers**: Manage OPDS libraries. See [OPDS Servers (Multiple Libraries)](#375-opds-servers-multiple-libraries).

**Tools**:
- **Clock Settings** (submenu):
  - **Use Clock**: Enable the software clock. "ON" / "OFF"
  - **Clock Format**: "24h" / "12h"
  - **Timezone**: Select from a list of supported timezones (UTC, CET, EET, MSK, IST, AEST, EST, CST, MST, PST, and more)
  - **Detect Timezone**: Auto-detect timezone via IP geolocation (requires WiFi).
  - **Sync Time**: Sync the clock via NTP (requires WiFi).
- **Weather Settings**: Configure the Open-Meteo weather panel shown on the Home Screen.

**System**:
- **Clear Reading Cache**: Clear the internal SD card cache.
- **Repair Screen**: Clears ghosting left behind by fast page refreshes, by driving every pixel hard between black and white several times. Takes about 20 seconds and deletes nothing. A maintenance action, not a fix for ghosting while you read.
- **System Information**: Display device info (firmware version, hardware, memory, SD card).
- **Boot Diagnostics**: How this boot started, where the last sleep stopped, and the history pairing each sleep with the boot that followed it. One screenful, meant to be photographed into a bug report when the device fails to sleep or fails to wake.
- **Reading Statistics**: View reading stats (streaks, time read, pages/min, per-book ETA, sparkline history).

**Firmware Update**:
- **Check for Updates**: Check for and download Witch Reader firmware updates over WiFi.
- **Include Beta Updates**: Whether to include release-candidate builds in update checks. "ON" / "OFF"
- **SD Firmware Update**: Flash a firmware `.bin` file from the SD card.
- **Switch to USB Drive**: Reboot the device into USB mass-storage mode to access the SD card directly from a computer.

#### 3.7.5 OPDS Servers (Multiple Libraries)

Witch Reader supports saving multiple OPDS servers and switching between them when browsing catalogs.

1. Open **Settings -> System -> OPDS Servers**.
2. Select **Add Server** to create a new entry, or select an existing server to edit it.
3. Configure these fields:
   - **Server Name**: Optional display name (for example, "Home Calibre" or "Public Catalog").
   - **OPDS Server URL**: Full catalog root URL (for Calibre Content Server, usually ends with `/opds`).
   - **Username / Password**: Optional credentials for authenticated servers.
4. Use **Delete Server** inside a server entry to remove it.

Behavior notes:

- You can store up to 8 OPDS servers.
- OPDS authentication supports HTTP Basic auth. If you use Calibre Content Server with authentication enabled, set it to Basic (not Digest).

You can also manage OPDS servers from the web interface while in File Transfer mode:

1. Connect to the device web UI.
2. Open `http://<device-ip>/settings`.
3. Use the **OPDS Servers** card to add, edit, or delete entries.
For web-based WiFi network management, see [Web Settings (WiFi + OPDS)](#376-web-settings-wifi--opds).

#### 3.7.6 Web Settings (WiFi + OPDS)

While in **File Transfer** mode, the web settings page includes management cards for both **WiFi Networks** and **OPDS Servers**.

1. On device: open **File Transfer** and connect to WiFi.
1. In a browser, open `http://<device-ip>/settings` or `http://witchhunt.local`.
1. In **WiFi Networks**, add, edit, or delete saved network entries (SSID + optional password).
1. In **OPDS Servers**, add, edit, or delete OPDS catalogs.

Behavior notes:

- Passwords are never shown back in the web UI after saving.
- Leaving Password blank while editing keeps the existing saved password unchanged.
- The web UI can save hidden-network SSIDs, but connecting to hidden networks still depends on device-side WiFi connection flow.

#### 3.7.7 KOReader Sync Quick Setup

Witch Reader can sync reading progress with KOReader-compatible sync servers automatically and bidirectionally. It also interoperates with KOReader apps/devices when they use the same server and credentials.

##### Option A: Free Public Server (`sync.koreader.rocks`)

1. Go to **Settings → System → KOReader Sync**.
2. Set **Sync Server URL** to `https://sync.koreader.rocks` (or leave it empty — the default points to the same server).
3. Enter your **Username** and **Password**.
4. Select **Register** to create a new account on the server — Witch Reader handles the registration on-device, including the required MD5 password hashing. If the username is already taken, choose a different one and try again.
5. Once registration succeeds, select **Authenticate** to confirm the credentials are working.

Already have KOReader Sync credentials? Skip **Register** and go straight to **Authenticate**.

##### Option B: Self-Hosted Server (Docker Compose)

1. Start a sync server on your computer or home server:

```bash
mkdir -p kosync-quickstart && cd kosync-quickstart

cat > compose.yaml <<'YAML'
services:
  kosync:
    image: koreader/kosync:latest
    ports:
      - "7200:7200"
      - "17200:17200"
    volumes:
      - ./data/redis:/var/lib/redis
    environment:
      - ENABLE_USER_REGISTRATION=true
    restart: unless-stopped
YAML

docker compose up -d
```

> [!NOTE]
> Set `ENABLE_USER_REGISTRATION=false` after creating your accounts to prevent unexpected registrations.

2. On the device, go to **Settings → System → KOReader Sync**:
   - Set **Sync Server URL** to `http://<server-ip>:17200` (or `https://<server-ip>:7200` for the TLS listener).
   - Enter your **Username** and **Password**.
   - Select **Register** to create the account directly from the device.
   - Select **Authenticate** to confirm.

##### Using sync while reading

Press **Confirm** to open the reader menu, then select **Sync Progress**:
- **Apply Remote** — jump to the progress stored on the server.
- **Upload Local** — push the current position to the server.

### 3.8 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode | Behavior |
|------|----------|
| **Dark** (default) | The Witch Reader logo on a dark background. |
| **Light** | The Witch Reader logo on a white background. |
| **Custom** | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found. |
| **Cover** | The cover of the currently open book. Falls back to **Dark** if no book is open. |
| **Cover + Custom** | The cover of the currently open book. Falls back to **Custom** behavior if no book is open. |
| **Page Overlay** | A transparent PNG composited over the current reader page — book content shows through the alpha channel. |
| **Quick Resume** | A minimal screen; waking the device returns to reading immediately. |
| **None** | A blank screen. |

The **Sleep Image Pick Mode** setting controls whether custom images are chosen **randomly** or **sequentially**.

An optional **tint overlay** (Off / White / Gray / Black) can be applied on top of the sleep image to dim or tint it.

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), **Inverted** (inverted black & white), **Adaptive** (stretches the picture between its own darkest and lightest points), or **Equalize** (spreads the tones by how much of the picture carries them).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom** or **Cover + Custom**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` or `.png` images inside. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. Used as fallback if no valid images are found in the `.sleep`/`sleep` directory.

> [!TIP]
> For best results:
> - Use PNG (with alpha channel for Page Overlay mode) or uncompressed BMP files with 24-bit color depth.
> - Use a resolution of 480×800 pixels to match the device's screen resolution.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning
| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Volume Up**    |
| **Next Page**     | Press **Right** _or_ **Volume Down** |

The role of the volume (side) buttons can be swapped in the **[Controls Settings](#373-controls)**.

If the **Short Power Button Click** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

### Chapter Navigation
* **Next Chapter:** Press and **hold** the **Right** (or **Volume Down**) button briefly, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Volume Up**) button briefly, then release.

This feature can be disabled in the **[Controls Settings](#373-controls)** to help avoid changing chapters by mistake.


### System Navigation
* **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
* **Return to Browse Files:** Press and hold the **Back** button to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen.
* **Reader Menu:** Press **Confirm** to open the reader menu, which includes: **[Table of Contents](#6-chapter-selection-screen)**, bookmarks, sync progress, reading statistics, quick per-book overrides (font, images, hyphenation, bionic reading…), take screenshot, and more.

### Supported Languages

Witch Reader renders text using the following Unicode character blocks, enabling support for a wide range of languages:

*   **Latin Script (Basic, Supplement, Extended-A):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, and others.
*   **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.

What is not supported: Chinese, Japanese, Korean, Vietnamese, Hebrew, Arabic, Greek and Farsi.

---

## 5. Touch Controls

*Applies to the touch devices only — the **LilyGo T5 S3 Pro** and the **Xteink X4 Pro**.
The X3 and X4 have no digitiser, nothing in this section appears in their Settings, and
the code behind it is not even built into their firmware.*

Touch is an addition to the buttons, never a replacement: every button still does exactly
what it did, and everything described here can be switched off.

Two things are worth knowing before the tables:

- **What each zone does is a setting; where the zones are is not.** Every gesture below
  can be reassigned under **Settings → Controls → Gesture actions**, using the same list
  of actions a physical button offers.
- **The device can show you its own answer.** **Settings → Controls → Gesture overview**
  draws the zones with *your* current assignments in them. It is generated from your live
  settings, so it is right even when this page is out of date — trust it over this text.

### 5.1 The two master switches

Both live under **[Settings → Controls](#373-controls)**:

| Setting | Governs | Default |
| --- | --- | --- |
| **Touch Navigation** | Everything **outside** a book: list rows, book covers, the home menu, the settings tabs, the on-screen button hints, the edge gestures | On |
| **Touch Page Turn** | The **reading page** itself: how touch turns pages | Tap Zones |

They are separate on purpose. Turning off page-turn taps so a resting thumb cannot flip a
page should not also stop you tapping a book in the library, and vice versa.

Neither can strand you:

- With **Touch Page Turn** off, the reader menu is still reachable — by the swipe up from
  the bottom edge, by the centre tap, and by the Confirm button.
- With **Touch Navigation** off, the menus are button-only. On these devices Back and
  Confirm come from the capacitive Home key, and the firmware feeds those in as *button*
  presses, below the level this setting acts on.

### 5.2 Menus, lists and everything outside a book

Outside a book, **taps belong to whatever is on screen** — a row, a cover, a keyboard key,
a button hint. Only swipes and corner holds are treated as gestures there.

**Lists use point-then-confirm.** The first tap on a row *moves the highlight* to it and
stops. A second tap on the row that is already highlighted *opens* it. A mis-tap therefore
costs one more tap instead of an action to undo, and on e-paper the highlight moving is
the only feedback there is. If you would rather a row opened on the first tap, set
**Settings → Controls → Tap Action** to *Activate immediately*.

**Tabs are a single tap.** Tapping Display / Reader / Controls / System in Settings, or
Navigation / Settings / Sync / Tools in the reader menu, switches category immediately —
you can already see which tab you are choosing, so there is nothing to confirm.

**The button hints are tappable.** The labelled boxes along the bottom edge, and the side
hints, do exactly what the physical button under each label does — including any remapping
you have configured. *Holding* a hint is the same as holding that button.

| Gesture | What it does |
| --- | --- |
| Tap a row, cover, folder or button hint | Select it; tap again to open it |
| Hold a button hint | The same as holding that button |
| Swipe up / down over a list | Page the list |
| **Tap the scroll bar** above / below the thumb | Page back / forward |
| Swipe **right from the left edge** | Back |
| Swipe **down from the top edge** | Reading light panel |
| **Hold the top-left corner** | Toggle the reading light — the same as in a book |

The scroll-bar strip is wider than the thin bar you can see, so you do not have to hit it
precisely. Tapping the thumb itself does nothing.

> The LilyGo T5 S3 has a **Down** key but no **Up** key, so paging a list *backward* has no
> physical button there — the scroll bar and the swipe are how you do it. They work on the
> X4 Pro as well, which has both keys.

### 5.3 The reading page: taps and holds

The page is divided into three columns. The outer columns turn pages over their whole
height; the middle column is split into three.

```
 +-----+---------------------+-----+
 |     |        Top          |     |
 |     +---------------------+     |
 | Prev|       Centre        | Next|
 | page|                     | page|
 |     +---------------------+     |
 |     |       Bottom        |     |
 +-----+---------------------+-----+
```

| Gesture | Default |
| --- | --- |
| Tap left column | Previous page |
| Tap right column | Next page |
| Tap centre | Reader menu |
| Tap top / bottom | *(nothing — reserved for vertical gestures)* |
| Hold left / right column | Previous / Next chapter |
| Hold centre | Look the word up in the dictionary |
| Hold bottom | Star this page |
| Hold top | *(nothing)* |

**Touch Page Turn** decides what the two outer columns do:

| Mode | What the page does |
| --- | --- |
| **Tap Zones** *(default)* | Tap the left third for the previous page, the right third for the next |
| **Tap Zones (Inverted)** | The same, mirrored |
| **Swipe** | Horizontal swipes turn pages; the taps stay free |
| **Off** | The page ignores touch for page turns |

The centre tap for the reader menu has no switch of its own. To turn it off, bind
**Tap centre** to *Ignore (do nothing)* in *Gesture actions* — the bottom-edge swipe and
the Confirm button still reach the menu.

### 5.4 The reading page: swipes

**A vertical swipe is decided by where your finger starts, not where it ends.** Put your
finger on the control you want, then move; you never have to judge distance. A swipe needs
about 60 px of travel to count as a swipe rather than a tap.

```
 +---------------------------------+
 |    top edge: reading light      |
 +-----+---------------------+-----+
 |     |                     |     |
 |Bright|   < 10 pages >     |Warmth|
 | ness |                    |      |
 |     |                     |     |
 +-----+---------------------+-----+
 |   bottom edge: reader menu      |
 +---------------------------------+
```

| Gesture | Default |
| --- | --- |
| Swipe **down** from the top edge | Reading light panel |
| Swipe **up** from the bottom edge | Reader menu |
| Swipe up / down in the **left edge** column | Light brighter / dimmer |
| Swipe up / down in the **right edge** column | Light warmer / cooler |
| Swipe **left** inside the left (back) column | Skip 10 pages back |
| Swipe **right** inside the right (forward) column | Skip 10 pages forward |
| Swipe left / right elsewhere | Next / Previous page *(in Swipe mode only)* |

The left and right edge columns stop short of the top and bottom bands, so a swipe starting
in a corner cannot mean two things at once. A vertical swipe down the **middle** of the page
does nothing on purpose — it stops a thumb resting mid-page from dimming the screen.

The ten-page skips read as one rule with the taps beside them: the left column already means
*back*, so a tap there goes back one page and a flick the same way goes back ten.

### 5.5 Corners, and two fingers

**The four corners answer only to a hold, never a tap**, so adding them changed the meaning
of no tap anywhere. Each corner is one eighth of the screen's *shorter* side, which keeps it
square and the same physical size whichever way you hold the device.

| Gesture | Default |
| --- | --- |
| **Hold the top-left corner** | **Toggle the reading light** |
| Hold the other three corners | *(nothing — free for you to assign)* |

The top-left corner hold works on **every screen**, not just while reading. One gesture, one
meaning, wherever you are — a reader should not have to know which screen they are on to turn
the light off.

> The reading light is the one control the brightness swipes cannot reach: swiping down only
> dims to a minimum, and swiping up on a dark screen turns the light *on*. Turning it **off**
> needs its own gesture, and a corner hold is deliberate enough not to fire while you shift
> your grip.

**Double-press the Power button** for the same thing without touching the screen at all. A
single press still puts the device to sleep, so nothing about the button's usual job changes.
This is the route to reach for in the dark: no overlay can cover a physical button.

Two-finger gestures work on the devices whose touch controller reports more than one finger,
which is both of them:

| Gesture | Default |
| --- | --- |
| Pinch in / out | Smaller / Larger text |
| Rotate clockwise / anticlockwise | Change orientation forward / back |

Turning two fingers back the way they came undoes the rotation rather than advancing three
more steps.

### 5.6 Reading light

*Devices with a frontlight or backlight only.*

Under **Settings → Display → Reading light**:

- **Reading Light** — on / off
- **Brightness** — 1–100%. The scale is perceptual, so the low end has as many usable steps
  as the top; there is deliberately no 0%, because that is what the on/off switch is for
- **Warmth** — 0 (fully cool) to 100 (fully warm). Only on a device with a second warm
  channel. Brightness is the *total* light and warmth splits it between the two LED strings,
  so changing it shifts the colour without changing how bright the page looks
- **Restore Light on Wake** — whether waking brings the light back. Brightness and warmth are
  always remembered; this only controls the on/off state

Both sliders **preview as you move them**, so you can judge the level by looking at the page
rather than at a number. Cancel puts back what you had. If the light was off when you opened
the slider, previewing lights it and both Cancel *and* Confirm put it back off — asking for a
brightness is not asking for the light to come on.

The quickest routes to it without opening Settings:

- **Swipe down from the top edge** for the light panel, on any screen
- **Hold the top-left corner**, or **double-press Power**, to toggle it
- **Swipe up / down at the left edge** to brighten and dim, and at the right edge for warmth

Adjusting brightness while the light is off turns it on — a brightness gesture always means
"I want light". These all work on the home screen and in menus as well as in the reader, so
you can find a book in the dark.

The same actions are bindable to any button or gesture: **Toggle Reading Light**, **Light
Brighter**, **Light Dimmer**, and on a warm/cool device **Light Warmer** and **Light Cooler**.

### 5.7 Per-device differences

| | X4 Pro | LilyGo T5 S3 |
| --- | --- | --- |
| Physical keys | Up, Down, Power | Down, Power |
| Back / Confirm | Home key: hold / tap | Home key: hold / tap |
| Top edge, swipe down | Reading light | Reading light |
| Bottom edge, swipe up | Reader menu | Reader menu |
| Right edge, swipe up/down | Light warmer / cooler | *(free — single-channel light)* |

On a touch device **without** a reading light the two vertical edges swap roles: the top edge
becomes the reader menu, there being no light panel to put there, and the bottom edge returns
Home.

### 5.8 Turning it off

- **Settings → Controls → Touch Page Turn** → *Off* stops touch turning pages, and nothing
  else.
- **Settings → Controls → Touch Navigation** → *Off* silences touch outside the reader.
- **Settings → Controls → Tap Action** switches list rows between select-then-open and
  open-on-first-tap.
- Any single gesture can be set to **Ignore (do nothing)** in *Gesture actions* to switch just
  that one off. This is different from **Built-in**, which means "leave this to the screen" —
  each row names what Built-in will actually do, so "Swipe left" reads *Next Page* while you
  are in Swipe mode and *Nothing* when you are not.
- The action **Toggle Touch Navigation** can be bound to a button, if you want to silence
  touch without going into Settings.

### 5.9 If a gesture does nothing

1. Open **Settings → Controls → Gesture overview** and check what that zone is actually
   assigned to. A gesture showing *Built-in* does whatever the reader would have done anyway,
   which for most zones is nothing.
2. A vertical swipe must **start** in the edge column or band it belongs to. Starting
   mid-page is unassigned by design.
3. A swipe needs about 60 px of travel to register as a swipe rather than a tap.
4. Light and warmth actions do not appear on a device without that hardware, and two-finger
   actions do not appear on a controller that reports a single finger. The overview page only
   ever shows what your device can do.
5. Corner zones answer to a **hold**, not a tap.

For the developer-facing account of how this was built and why each default was chosen, see
[`docs/touch-gestures.md`](docs/touch-gestures.md) and
[`docs/touch-input-migration-2026-08-14.md`](docs/touch-input-migration-2026-08-14.md).

---

## 6. Chapter Selection Screen

Accessible by pressing **Confirm** while inside a book and selecting **Table of Contents**.

1.  Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to highlight the desired chapter.
2.  Press **Confirm** to jump to that chapter.
3.  *Alternatively, press **Back** to cancel and return to your current page.*

---

## 7. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features have known limitations:

* **Cover Images:** Large cover images embedded into EPUB require several seconds (~10s for ~2000 pixel tall image) to convert for the sleep screen and home screen thumbnail. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.
* **Right-to-left scripts (Hebrew, Arabic):** Not currently supported. For BiDi / RTL support, use the original [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) firmware.
* **CJK (Chinese, Japanese, Korean):** Not supported. See https://github.com/aBER0724/crosspoint-reader-cjk for a CJK-capable fork.

---

## 8. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Witch Reader, feel free to raise an issue ticket and attach the serial monitor logs. The logs can be obtained by connecting the device to a computer and starting a serial monitor. Either [Serial Monitor](https://www.serialmonitor.org/) or the following command can be used:

```
pio device monitor
```

If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot to the Home Screen.

There can be issues with broken cache or config. In this case, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.json`, `state.json`, or `epub_*` cache directories in the `.crosspoint/` folder).
