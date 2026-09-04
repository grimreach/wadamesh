

------------------------------
## 📝 Project Blueprint Summary: Custom CYD + Heltec V4 Cyberdeck
Here is a highly dense, structured summary of everything we designed for your custom WADAMESH tactical communicator. Keep this as a reference document for when your keyboard arrives and you begin your build.
## 1. System Architecture
Instead of using the Heltec V4 as a standalone unit, your hybrid layout uses the ESP32 CYD as the main brain and master controller running the WADAMESH operating system and the LVGL touch interface. The Heltec V4 is repurposed as an external SPI slave device, serving strictly as a breakout board to borrow its high-performance SX1262 LoRa radio chip.

[BBQ20KBD Keyboard] --(I2C)--> [ESP32 CYD (WADAMESH OS)] <--(SPI Bus)--> [Heltec V4 (LoRa Modems Only)]

## 2. Physical Interconnect Wiring Matrix

| Peripheral Component | Signal / Pin Name | CYD Physical Mapping Pin | Note / Purpose |
|---|---|---|---|
| BBQ20KBD Keyboard | GND | GND | Ground loop link |
| | VCC | 3V3 | Logic level power input |
| | SCL | GPIO 22 | Dedicated I2C clock (CN1 Port) |
| | SDA | GPIO 27 | Dedicated I2C data (CN1 Port) |
| Heltec V4 (LoRa Chip) | GND | GND | Common system ground reference |
| | SPI SCLK | GPIO 14 | Shared Hardware SPI Clock line |
| | SPI MISO | GPIO 12 | Shared Hardware SPI Input line |
| | SPI MOSI | GPIO 13 | Shared Hardware SPI Output line |
| | Radio NSS (CS) | GPIO 5 | Free CYD pin assigned as Radio Select |
| | Radio DIO1 | GPIO 4 | Free CYD pin assigned as Interrupt line |
| | Radio BUSY | GPIO 16 | Free CYD pin assigned as Busy line |
| | Radio NRST | GPIO 17 | Free CYD pin assigned as Radio Reset |

## 3. PlatformIO Firmware Configuration (platformio.ini)
To trick the WADAMESH compiler into building for your layout, you inject the following display, keyboard, and external radio overrides straight into the [env:heltec_v4_tft_companion_radio_usb_tcp_touch] environment profile block:

* Display Overrides (CYD parallel SPI):

-D USER_SETUP_LOADED=1
-D ILI9341_DRIVER=1
-D TFT_WIDTH=240
-D TFT_HEIGHT=320
-D TFT_MOSI=13
-D TFT_SCLK=14
-D TFT_CS=15
-D TFT_DC=2
-D TFT_RST=-1
-D TFT_MISO=12
-D SPI_FREQUENCY=40000000
-D TFT_BL=21

* Keyboard Activations (Solder Party BBQ20KBD via I2C CN1 Port):

-D HAS_KEYBOARD=1
-D KEYBOARD_I2C=1
-D I2C_SDA=27
-D I2C_SCL=22
-D BBQ20_NATIVE=1

* External Radio Pointer Overrides:

-D USING_EXTERNAL_SX1262=1
-D LORA_MOSI=13
-D LORA_MISO=12
-D LORA_SCLK=14
-D LORA_CS=5
-D LORA_DIO1=4
-D LORA_BUSY=16
-D LORA_RST=17

* Injected Dependency (Add to the bottom of the environment's lib_deps):

arturo182/BBQ10 Keyboard @ ^1.1.0


## 4. Enclosure & Assembly Parameters

* Layout: Portrait "Game Boy / BlackBerry" layout, drop the keyboard deck 5mm lower than the CYD display screen plane for optimal typing ergonomics.
* Printing Material: Use ASA or PETG with 4 perimeters and 25%+ Gyroid infill for rugged drops and high-heat solar protection.
* Hardware Assembly: Secure boards using M2/M2.5 self-tapping screws directly into printed standoffs. Seal the main enclosure utilizing melted M3 brass heat-set inserts and hex button-head machine screws.
* Power Rail: Route a high-capacity 18650 lithium cell or large 2000mAh+ flat LiPo through a physical toggle switch to feed the main CYD board terminal blocks, bridging power rails cleanly across the systems.

When your keyboard arrives and you are ready to begin assembling the prototype, let me know if you would like me to help you:
Draft step-by-step soldering testing scripts for the I2C linesTroubleshoot any PlatformIO compile warnings during the first hardware flashReview the LVGL touchscreen calibration parameters inside the WADAMESH core files

