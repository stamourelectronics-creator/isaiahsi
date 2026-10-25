# IsaiahAI 6.0 for Flipper Zero

IsaiahAI 6.0 combines an ESP32 GPIO/UART AI chat bridge with a Bluetooth Low Energy HID keyboard workflow for typing messages on a paired iPhone.

## Bluetooth message workflow

1. Install the FAP in `SD Card/apps/Tools/` and launch IsaiahAI 6.0.
2. Select **Connect iPhone**. On the iPhone, open **Settings → Bluetooth** and pair with the keyboard advertised by the Flipper.
3. Open the intended conversation in Messages and tap the message composition field.
4. Return to the Flipper, choose **Type & Send Message**, enter the message, and confirm.
5. The Flipper types basic ASCII text over Bluetooth and presses Return. Depending on iOS/app settings, Return may insert a line break rather than send; if so, tap **Send** on the iPhone.

This is Bluetooth keyboard emulation, not direct iMessage access. iOS does not let a generic Bluetooth keyboard app read Messages, choose a recipient, or guarantee sending in the background. Unicode/emoji and non-English keyboard layouts are not supported by this first version.

## AI chat bridge

The app sends `ASK:<prompt>\n` over UART at 115200 baud and expects a single line `REPLY:<answer>\n` from the ESP32.

Wiring: ESP32 TX → Flipper GPIO pin 14 (RX), ESP32 RX → Flipper GPIO pin 13 (TX), and GND → GND. Verify the ESP32 board's UART voltage levels before connecting.

## SD-card data

Chat history and contacts are stored in the IsaiahAI app-data files. The app does not format the SD card or intentionally erase unrelated files.
