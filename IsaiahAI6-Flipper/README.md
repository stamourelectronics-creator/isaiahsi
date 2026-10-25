# IsaiahAI 6.0 for Flipper Zero

This folder contains an external Flipper app that sends prompts to an ESP32 bridge over GPIO UART.

## Build
A GitHub Actions workflow builds the app on pushes to the `isaiahai6-flipper-fap` branch. Open the repository's **Actions** tab, select **Build IsaiahAI 6.0 Flipper FAP**, open the successful run, and download the `IsaiahAI6-Flipper-FAP` artifact ZIP. Extract it to obtain the compiled `.fap`.

## Install
In qFlipper, open **File Manager → SD Card → apps → Tools** and copy the compiled `.fap` there. Launch it from **Apps → Tools**.

## GPIO bridge protocol
The app sends `ASK:<prompt>\n` at 115200 baud. It expects a single response line in the form `REPLY:<answer>\n`.

Wiring: ESP32 TX → Flipper GPIO pin 14 (RX), ESP32 RX → Flipper GPIO pin 13 (TX), and GND → GND. Verify the ESP32 board's UART voltage levels before connecting.

## SD-card data
The app appends chat history and contacts to its own app-data files. It does not format the SD card or intentionally erase unrelated files.

## Important limitations
This is a first-pass source build. The GitHub Actions build result is the authority on whether it compiles against the current release SDK. Phone messaging and iMessage integration are not implemented in this Flipper app.
