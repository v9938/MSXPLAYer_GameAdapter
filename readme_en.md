# MSXPLAYer Game Cartridge Adapter

[日本語版へ](./readme.md)

A tool for reading and writing MSX cartridges via USB CDC (virtual COM port). (MSXPLAYer support planned)

![title00](./IMAGE/title_fixed000.jpg)

![title01](./IMAGE/title_fixed001.jpg)

## What is this?

A cartridge adapter for reading and writing **MSX ROM cartridges** using a Raspberry Pi-based microcontroller (RP2350).  
By sending commands from a PC via **USB CDC (serial)**, you can perform the following operations:

- Read/Write ROM cartridges
- IO Read/Write
- Bulk transfer to/from the buffer (64KB) and the PC
- Batch slot access using simple scripts stored in the buffer

## Features

- **USB CDC (virtual COM)** command operation (generally no driver required; available on Windows / Linux / macOS)  
- Built-in **slot power control and overcurrent detection**
- Supports **Read/Write** access to cartridge memory and IO space
- Supports **clock signals**

- **5V IO voltage compatible**
- **Firmware can be updated by users**
- **Low-latency design** utilizing a **dual-core configuration**  
  - Core 0: USB reception, command parsing, transmission queue output  
  - Core 1: Command execution (GPIO access, etc.)
- **Command queue functionality** for continuous command execution
- Includes a **simple script engine** (simple VM)

(The following applies to distributed items)

- Uses **reliable slot connector parts from major manufacturers (AMP/Hirose)**

## Differences from actual MSX cartridge slots

Unlike actual MSX machines, the following features are **not** supported:

- +12V/-12V power supply
- Sound output
- DRAM refresh signal support
- M1 signal support

## Device Description

![pcb001](./IMAGE/pcb001.jpg)

1. MSX SLOT: The slot where MSX-standard cartridges are inserted.  
2. USB-C Port: The port used to connect to a PC.  
3. GROVE Port: A GROVE-compatible communication port. The signal voltage level is 3.3V. Currently outputs debug UART signals.  
4. ACCESS LED: An LED that indicates cartridge access status. It lights when power is supplied and during cartridge access.  
5. BOOT Switch: Not normally used.

## [Important] MSXPLAYer Support

Support for MSXPLAYer is currently being developed with the MSX Association.  
Our distributed version is planned to include MSXPLAYer, but details are still undecided at this stage.

For the time being, this will be an Early Access version distributed to testers who already own MSXPLAYer and can cooperate with operation testing.

Therefore, the following book is required to obtain MSXPLAYer:

**"MSX-BASICでゲームを作ろう　懐かしくて新しいMSXで大人になった今ならわかる"**  
(Create Games with MSX-BASIC: Understanding MSX Now That We're Grown Up)

<https://www.amazon.co.jp/dp/4297148900/>  
<https://books.rakuten.co.jp/rb/18203653/>  
<https://www.yodobashi.com/product/100000009004112958/>

We plan to support it by applying patches to the MSXPLAYer obtained through this book.  
We are also preparing a bug report form for cases where problems occur on the MSXPLAYer side.

Specific information on how to obtain the compatible version will be added after distribution begins.

## Support for other platforms such as OpenMSX

We are looking for volunteers who can help. We would be happy to discuss it via DM on X.

## Distribution

This is currently an Early Access version.  
It is distributed to testers who can cooperate with operation testing and the development of compatible software.  
Please purchase only if you understand that MSXPLAYer is required separately and that specifications may differ from those of future official versions.

Simple assembly work using a screwdriver is required.  
The connector kit version also requires simple soldering work for the slot section.

### Distribution Sites

Two versions are available: a soldering kit version, in which the slot requires soldering, and a completed PCB version.  
Please note that prices may fluctuate significantly for some time depending on global manufacturing conditions.

#### Booth

Available from: 2026/5/10 12:00 (JPT)  
<https://ifc.booth.pm/items/8175544>

Completed PCB version: 7,060 JPY / Soldering kit version: 6,560 JPY

## Included Items

1. Game Cartridge Adapter PCB  
2. Dedicated aluminum panel  
3. Rubber feet  
4. Spacers x 4  
5. Screws (3mm x 8mm) x 8  
6. LED light pipe  
7. 50-pin card edge connector (soldering kit version only)

![PARTS](./IMAGE/parts_image.jpg)

## Assembly Instructions

### 1. Solder the Card Edge Connector (Soldering kit version only)

Solder the card edge connector. The connector has no specific direction.  
When attaching it, be careful not to leave any gap between the connector and the PCB.  
![assembly1](./IMAGE/assembly001.jpg)

It is recommended to first solder both end terminals, check that it is seated properly, and then solder the remaining terminals.  
![assembly2](./IMAGE/assembly002.jpg)

If the included connector is made by Hirose, the leads are slightly too long as-is, so trim the excess using wire cutters.  
![assembly3](./IMAGE/assembly003.jpg)

### 2. Attach the Light Pipe

Press the light pipe into the panel. Insert the flat side first from the front surface of the panel, and push it in until there is no gap.  
![assembly4](./IMAGE/assembly004.jpg)
![assembly5](./IMAGE/assembly005.jpg)

### 3. Attach the Spacers (Front Side)

Attach the spacers by fastening screws from the front side of the panel.  
Since final adjustment will be done later, it is better to tighten them only lightly at this stage.  
![assembly6](./IMAGE/assembly006.jpg)

### 4. Attach the Spacers (Back Side)

Place the PCB on top of the spacers and secure it with screws. Be careful about the orientation of the PCB.  
![assembly7](./IMAGE/assembly007.jpg)
![assembly8](./IMAGE/assembly008.jpg)

After tightening the four screws on the back side, fully tighten the four screws on the front side as well.  
![assembly9](./IMAGE/assembly009.jpg)

### 5. Attach the Rubber Feet

Attach the rubber feet to four locations on the back side of the PCB near the screws.  
![assembly10](./IMAGE/assembly010.jpg)

This completes all assembly work. Thank you for your effort.

## Operation Check (for Windows)

Please download the file below, insert an MSX cartridge into this device, and run the program.  
If it is working correctly, the contents of the cartridge starting from address `0x4000` will be displayed as shown below.

**Operation Check Program** [MSXPLAYer Game Cassette Adapter Operation Check Program](./SOFTWARE/TestProgram/)

![Operation Check Screen](./IMAGE/testprg000.png)

---

## Technical Documentation

### USB CDC Command Specifications

[MSXPLAYer Game Cassette Adapter Command Specifications](./com_command_en.md)

### Sample Programs

For Windows: (Verified with VC++2019/2026)

- **Game Cartridge Dump Sample (requires FW 260531 or later):** [ROM Cartridge Dump Program](./SOFTWARE/MSXCR_ROMDUMPER_FW260531/)
- **Game Cartridge Dump Sample (requires FW 260520 or later):** [ROM Cartridge Dump Program](./SOFTWARE/MSXCR_ROMDUMPER_FW260519/)
- **Game Cartridge Dump Sample:** [ROM Cartridge Dump Program](./SOFTWARE/MSXCR_ROMDUMPER/)
- **Script Engine Sample:** [MSX_SimpleCartridge Write Program](./SOFTWARE/SimpleROM64KWriter/)  
  Compatible cartridge: [https://github.com/v9938/MSX_SimpleCartridge](https://github.com/v9938/MSX_SimpleCartridge)

### Other Programs

- **MSX-PLAYer-GCA-Reader by Lithelia:** [https://github.com/Lithelia/MSX-PLAYer-GCA-Reader](https://github.com/Lithelia/MSX-PLAYer-GCA-Reader)  
- **mgadump by madscient:** [https://github.com/madscient/mgadump](https://github.com/madscient/mgadump)  
- **MSXPLAYer Game Cartidge Adapter GUI by t-bucchi:** [https://github.com/t-bucchi/msx-cartrigde-adapter-gui](https://github.com/t-bucchi/msx-cartrigde-adapter-gui)

### How to Use MSXCR_ROMDUMPER_FW260531

This Windows tool uses the `SMTH` command added in firmware version 260520 and later  
to dump a ROM while also obtaining its HASH value during the read process.  
In version 260531, the bank handling and related processing were revised  
to improve both speed and operational stability.

#### Command Line

- Normal mode with an explicitly specified output file name

  ```bat
  MSXCR_ROMDUMPER.exe dump.rom
  ```

- Automatic file naming mode with an explicitly specified output directory

  ```bat
  MSXCR_ROMDUMPER.exe /AUTO
  ```

  or

  ```bat
  MSXCR_ROMDUMPER.exe /AUTO .\OUT
  ```

#### Description of Each Mode

**Normal Mode**  
The ROM is saved using the file name specified on the command line.

**Automatic File Naming Mode (`/AUTO`)**  
The save file name is automatically determined based on ROM information and the HASH value.  
If a second argument is specified, the file is saved into that folder.  
If the second argument is omitted, the file is saved into the current folder.

#### About `msxromdb.xml` / `softwaredb.xml`

This software performs automatic matching using the ROM database from BlueMSX / OpenMSX.  
Place the file in the same folder as the executable using the name `softwaredb.xml` or `msxromdb.xml`.  
If this file exists, the ROM DB information is used for title identification and file name determination.

The XML-format ROM database can be obtained from the BlueMSX installation directory or from the site below.  
[https://romdb.vampier.net/downloads.php](https://romdb.vampier.net/downloads.php)

#### About Output File Names

In **Normal Mode**, the ROM is saved exactly with the file name you specify.  
In **Automatic File Naming Mode**, the ROM is saved using a name based on the ROM identification result.  
If `softwaredb.xml` / `msxromdb.xml` is available, the file name is determined primarily using the information registered in the ROM DB.  
If the ROM DB is unavailable or no matching entry is found, an automatically generated name based on ROM size, HASH value, and similar information is used.  
If a file with the same name already exists in the destination, the existing file is read and compared.  
If the contents are identical, `[same_+hash]` is added to the beginning of the file name.  
If the contents are different, `[other_+hash]` is added to the beginning of the file name.  
If the dump is presumed to have failed, `[unsuccessful]` is added to the beginning of the file name.

## Verified ROM Cartridges

[Verified ROM Cartridge List](./softlist.md)

## Firmware

Compiled firmware is available in the following folder:

[Firmware Location](./FIRMWARE/UF2)

## Firmware Update

There are two available methods.

- Using the BOOT switch  
  Press and hold the BOOT switch while inserting the USB cable to enter BOOT mode.  
  A drive named `RP2350` will appear; copy the firmware file above to it.

- Using the Firmware Update tool  
  We provide `msxcr_ffu.exe` as an FFU support tool.  
  Running the batch file `ffu.bat` in the FFU folder above will update the firmware.

### File Structure (Overview)

- `main.c`
  - USB CDC reception (line buffer → parsing)
  - Command queue (Core0 → Core1)
  - Slot Memory / IO Read/Write
  - BRCV reception (binary receive mode)
  - BSND transmission (binary transmission queue)
  - LED control (WS2812)
  - Factory Test (GPIO test)
  - Script Engine (`executeCommands`)
- `commands.c / commands.h`
  - Command name → execution function (`cmd_*`) table (public command list)
- `ports.c / ports.h`
  - GPIO definitions (`board_pins[]`)
- `ws2812.pio`
  - PIO program for WS2812 control (from SDK pico-examples)
- `pwm_low_hiz.pio`
  - PIO for slot clock (LOW → Hi-Z)
- `usb_descriptors.c / tusb_config.h`
  - USB CDC descriptor/buffer configuration

### Firmware Operation Overview (Data Flow)

1. A text command is sent from the PC via USB CDC (example: `SMRD,1000\r\n`)
2. Core0 accumulates input into `lineBuf` until a line break, parses it, and places it into `commandBufs[]`
3. Core1 searches `cmd_table[]` for the corresponding function and executes `cmd_*()`
4. The response is queued in `cdc_queue[]`, and `cdc_task()` on Core0 transmits it over USB

### Build

Compiled in a Visual Studio Code PIC-SDK2.20 environment.

## USB VID/PID

We use the VID/PID allocation of the former ASCII Corporation under permission from MSX License Corporation.  
If you manufacture a modified version of this product, please use a different VID/PID.

## Circuit Diagram

[Circuit Diagram PDF](./PCB/MSXPLAYerCR_1SLOT_RevD.pdf)  
![Circuit Diagram](./PCB/MSXPLAYerCR_1SLOT_RevD_SCR.png)

## PCB Data

[Rev D Gerber Data](./PCB/GARBER_DATA/)

![PCB Image](./PCB/MSXPLAYerCR_1SLOT_RevD_PCB.png)

## BOM List

[Rev D PCB Parts List](./PCB/partslist_RevD.md)

## Panel

[Aluminum Panel Data](./PCB/PANEL_DATA/)

![Panel Image](./PCB/PANEL_DATA/panel_revD.png)

The following additional parts are required for assembly:

| Part Name | Quantity | Source |
| --- | --- | --- |
| 3mm x 8mm Screw | 8 | <https://www.hirosugi-net.co.jp/shop/g/g104024/> |
| 3mm x 20mm Spacer | 4 | <https://www.hirosugi-net.co.jp/shop/g/g670/> |
| Light Pipe (VCC LFB075CTP) | 1 | <https://www.digikey.jp/ja/products/detail/visual-communications-company-vcc/LFB075CTP/5723594> |

## Regarding the MSXPLAYer Name / Logo

MSX and MSXPLAYer are registered trademarks of MSX License Corporation.

The MSXPLAYer logo attached to our distributed products is used with permission under cooperative development with the MSX Association.

## License

This project is published under the MIT License.

Note that some portions of the code are copied from Raspberry Pi's pico-sdk v2.20.  
Those portions are licensed by Raspberry Pi (Trading) Ltd. under the requirements of the BSD 3-Clause "New" or "Revised" License.

