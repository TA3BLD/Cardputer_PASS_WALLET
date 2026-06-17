# M5Cardputer Hardware Password Manager (AES-128 ECB)

A fully standalone, encrypted, offline password manager built for the M5Stack Cardputer.

## Features

* **Hardware Encryption:** Uses `mbedtls` for AES-128 ECB encryption. Passwords are never stored in plain text.
* **Offline Storage:** Stores encrypted credentials locally on an SD card in a JSON format (`/PSWRD/PASSWRD.json`).
* **USB HID Emulation:** Acts as a physical USB keyboard. Injects usernames and passwords directly into the host machine with a single keystroke.
* **QR Code Display:** Generates a high-capacity QR code (Version 7) on the Cardputer's screen containing both the username and password for rapid mobile scanning.
* **Random Password Generation:** Built-in hardware-seeded 32-character random password generator (includes symbols, letters, and numbers).
* **Master Password Protection:** Requires a master password to access the credential list and an additional verification step to reveal passwords in plain text on the screen.

## Prerequisites & Dependencies

* PlatformIO
* M5Cardputer Library (`^1.0.3`)
* ArduinoJson (`^7.0.4`)
* QRCode (`^0.0.1`)

## Installation

1. Clone this repository.
2. Open the project in PlatformIO.
3. Edit `main.cpp` and update the `masterPassword` and `aes_key` (must be exactly 16 bytes).
4. Format a MicroSD card (FAT32) and insert it into the Cardputer.
5. Build and upload to your M5Cardputer.

## Usage Guide

* **Boot:** Insert the SD card before powering on. The system will initialize the SPI interface and verify the card.
* **Login:** Enter the master password.
* **Navigation:** Use `;` (Up) and `.` (Down) to scroll through the credential list.
* **Add Credential:** Press `N` from the list view. Follow the prompts for Title, Username, and Password (choose `[M]` for manual entry or `[R]` to generate a random 32-character password).
* **Action Menu (Detail View):**
    * `[U]`: Type the Username via USB HID.
    * `[P]`: Type the Password via USB HID.
    * `[G]`: Reveal the password on the screen (requires Master Password verification).
    * `[Q]`: Display the QR code.
    * `[DEL]`: Go back.

## Companion Python Decoder

A Python script is included to manually decrypt the JSON file if necessary.

```bash
# Setup virtual environment
python3 -m venv venv
source venv/bin/activate

# Install dependency
pip install pycryptodome

# Run decoder
python python_codes/decoder.py
