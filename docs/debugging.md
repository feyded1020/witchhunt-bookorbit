# How to Capture Serial Logs from ESP32-C3

When submitting an issue or asking for debugging help, capturing and attaching the serial console logs from your ESP32-C3 device is often essential.

Below are step-by-step instructions for Windows, macOS, Linux, and Web Browsers. Choose the method that best matches your setup.

## 🌐 Method 1: Web Browser (Chrome / Edge) — *Easiest / All Operating Systems*

If you use **Google Chrome** or **Microsoft Edge**, you do not need to install any software or use command-line terminals. You can use Web Serial directly in your browser.

1. **Connect Device:** Plug your ESP32-C3 into your computer using a USB **data** cable (ensure it is not a power-only charging cable).

2. **Open Web Serial Monitor:** Go to a Web Serial tool in Chrome or Edge (e.g., [https://esp.nonet.eu/](https://esp.nonet.eu/?utm_source=gemini) or [https://serial.dazzler.app/](https://serial.dazzler.app/?utm_source=gemini)).

3. **Configure Settings:**

   * **Baud Rate:** Set to `115200`

4. **Connect:** Click **Connect** (or **Pair**), select your ESP32-C3 device from the browser pop-up window, and confirm.

5. **Capture Output:** Press the **Reset** button on your ESP32-C3 to capture the boot sequence and logs. Copy the output text and paste it into your GitHub issue.

## 🪟 Method 2: Windows

### Using PuTTY (Recommended)

1. **Find COM Port:**

   * Right-click the Windows Start menu and select **Device Manager**.

   * Expand **Ports (COM & LPT)**.

   * Look for `USB Serial Device`, `CH340`, or `CP210x` and note the COM number (e.g., `COM3`).

2. **Download PuTTY:** Download standalone `putty.exe` from [putty.org](https://www.putty.org/?utm_source=gemini).

3. **Configure PuTTY:**

   * Connection type: Select **Serial**.

   * **Serial line:** Enter your COM port (e.g., `COM3`).

   * **Speed:** Enter `115200`.

4. **Capture Logs:**

   * Click **Open**.

   * Press the **Reset** button on your ESP32-C3 device.

   * Right-click the PuTTY title bar and select **Copy All to Clipboard** to copy your log messages.

## 🍎 Method 3: macOS

1. **Open Terminal:** Press `Cmd + Space`, type `Terminal`, and press `Enter`.

2. **Identify Port:** Run the following command to list connected USB serial devices:

   ```
   ls /dev/tty.usb* /dev/tty.usbmodem*
   
   ```

   *Note the device path returned (e.g., `/dev/tty.usbmodem14101` or `/dev/tty.usbserial-1410`).*

3. **Start Serial Session:**
   Run `screen` with your device path and baud rate:

   ```
   screen /dev/tty.usbmodem14101 115200
   
   ```

4. **Capture Output:** Press the **Reset** button on your ESP32-C3 to view boot logs. Select and copy the text output.

5. **To Exit `screen`:** Press `Ctrl + A`, then type `:quit` and press `Enter`.

## 🐧 Method 4: Linux

1. **Identify Port:** Open a terminal and run:

   ```
   ls /dev/ttyUSB* /dev/ttyACM*
   
   ```

   *(Usually `/dev/ttyUSB0` or `/dev/ttyACM0`)*

2. **Check Port Permissions:**
   If you get a permission error when opening the port, add your user to the `dialout` group:

   ```
   sudo usermod -a -G dialout $USER
   
   ```

   *(You may need to log out and log back in for changes to take effect).*

3. **Monitor Output:**
   Use `screen`:

   ```
   screen /dev/ttyACM0 115200
   
   ```

   Or use direct stream reading:

   ```
   stty -F /dev/ttyACM0 115200 raw -echo && cat /dev/ttyACM0
   
   ```

## 🛠️ Troubleshooting Checklist

* **Garbage Characters / Gibberish Text:** Verify that your baud rate is set to **`115200`**.

* **No Device Found:** Ensure your USB cable supports **data transmission** and isn't a power-only charging cable.

* **Driver Needed (Windows):** If Device Manager shows an unknown USB device with a yellow icon, download and install the CH340 or CP210x drivers.