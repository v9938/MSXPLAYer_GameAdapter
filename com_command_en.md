# MSXPLAYer Game Cassette Adapter Command Specifications

## General Specifications

- 1 line = 1 command (comma-separated)
- Format (maximum 4 arguments):

  ```CMD
  CMD[,ARG1[,ARG2[,ARG3[,ARG4]]]]<CR|LF|CRLF>
  ```

- Command names are case-insensitive (converted to uppercase internally)
- Arguments are basically **hexadecimal** (`0x` prefix is allowed)
- If an argument is omitted, the default behavior is used.
- Return values:
  - Normally, `OK` / `FAIL` is returned as the final processing result.
  - During `ERROFF`, OK/FAIL display is suppressed, and only the success/failure counters are updated.

## Command List (Specification Format / Implementation Compliant)

### 1) HSET - Hardware Setting

- **Function**: Configures wait times and compatibility modes for hardware access.
- **Format**: `HSET,[Address],[Data]`
- **Arguments**:
  - `Address` : Setting item number (hexadecimal)
  - `Data` : Setting value (hexadecimal; if omitted, the setting returns to its default value)
- **Response**: `OK` / `FAIL`
- **Notes**:
  - The current firmware supports the following settings.

| Address | Setting Item | Description | Default |
|---|---|---|---|
| `0` | `MEMWAIT` | Wait time after Memory Read/Write (`n x 10ns`) | `0` |
| `1` | `RDWAIT` | `/RD` signal width during Memory Read (`n x 10ns`) | `100` |
| `2` | `WRWAIT` | `/WR` signal width during Memory Write (`n x 10ns`) | `18` |
| `3` | `P6MODE` | PC-6001 16KB mode setting (`0`: OFF / `1` or higher: ON) | `0` |

- **Examples**:
  - `HSET,1,64` : Sets `RDWAIT` to `1000ns = 1us`
  - `HSET,1` : Restores `RDWAIT` to its default value

- **Supplement**:
  - The current setting values can be checked with the `HINF` command.
  - The output of `HINF` includes `MEMWAIT` / `RDWAIT` / `WRWAIT` / `P6MODE`.
  - Specifying an unsupported `Address` results in `FAIL`.

---

### 2) ERROFF - Result Display OFF

- **Function**: Suppresses sequential display of command execution results (OK/FAIL display).
- **Format**: `ERROFF`
- **Arguments**: None
- **Response**: `OK`
- **Notes**:
  - When enabled, statistics are accumulated in `passCount/errCount`.
  - Used to suppress logs during bulk processing (transfers, scripts).

---

### 3) ERRON - Result Display ON + Summary

- **Function**: Cancels `ERROFF` and displays the statistics accumulated during the suppression period.
- **Format**: `ERRON`
- **Arguments**: None
- **Response**:
  - `PASS : <passCount>`
  - `FAIL : <errCount>`
  - Then returns `FAIL` if there were any failures; otherwise returns `OK`
- **Notes**:
  - If there were any failures, `ERRON` itself returns `FAIL`.

---

### 4) BCLR - Buffer Clear (slotMem Initialization)

- **Function**: Fills the internal buffer (64KB) with the specified value.
- **Format**: `BCLR(,[BufferAddress],[Length],[Data])`
- **Arguments**:
  - `BufferAddress` : Start position (defaults to 0 if omitted)
  - `Length` : Length (defaults to the full 64KB range if omitted)
  - `Data` : Fill value (defaults to `0xFF` if omitted)
- **Response**: `OK` / `FAIL`
- **Notes**:
  - Out-of-range values result in `FAIL`.
  - The length may be adjusted to fit the buffer boundaries.

---

### 5) BSND - Buffer Send Host (Device → Host Binary Send)

- **Function**: Sends data from the internal buffer to the PC in binary format.
- **Format**: `BSND,[BufferAddress],[Length]`
- **Arguments**:
  - `BufferAddress` : Start position for transmission
  - `Length` : Number of bytes to send
- **Response**:
  - Binary data is sent to the PC.
  - Then `OK/FAIL` is sent if `displayFlag=true`.
- **Notes**:

---

### 6) BRCV - Buffer Receive Host (Host → Device Binary Receive)

- **Function**: Receives binary data of the specified length from the PC and stores it in `slotMem[]`.
- **Format**: `BRCV,[BufferAddress],[Length]` + (followed by Length bytes of binary data)
- **Arguments**:
  - `BufferAddress` : Storage start position
  - `Length` : Number of bytes to receive
- **Response**:
  - `OK/FAIL` after reception is complete
- **Notes**:
  - Immediately after `BRCV` is executed, the device enters binary receive mode.
  - Out-of-range conditions such as `BufferAddress + Length` exceeding 64KB result in `FAIL`.

---

### 7) HVER - Hardware Version

- **Function**: Displays the hardware name, revision, and build date.
- **Format**: `HVER`
- **Arguments**: None
- **Response**:
  - `HW_NAME`
  - `HW_VERSION`
  - `FIRMWARE DATE`
  - `OK`
- **Notes**:
  - `HW_NAME`: Card reader name
  - `HW_VERSION`: Hardware version
  - `FIRMWARE DATE`: Firmware release date
  - The output format was changed in 260531 (V1.40).

---

### 8) HINF - Hardware Information

- **Function**: Displays hardware configuration information.
- **Format**: `HINF`
- **Arguments**: None
- **Response**: Each `KEY,VALUE` line + `OK`
- **Notes**:
  - `SLOTNUM`: Number of slots on the reader  
    Indicates the number of physical slots installed on the reader (1 to 4).
  - `SLOTPHY`: Physical slot arrangement information of the reader  
    Indicates the arrangement of physical slots on the reader (`1` = active).

    |bit7-4|bit3|bit2|bit1|bit0|
    |---|---|---|---|---|
    |0000|SLOT3|SLOT2|SLOT1|SLOT0|

    Example: `SLOTNUM:1 / SLOTPHY:2` = one physical slot installed, treated as `SLOT1`

  - `POWERCTRL`: Power control availability  
    Indicates whether power control for the cartridge is available (`1` = yes).
  - `CURRENTSENSOR`: Overcurrent detection availability  
    Indicates whether an overcurrent detection circuit for the cartridge is available (`1` = yes).
  - `PWR12V`: +12V/-12V power availability  
    Indicates whether +12V/-12V power for the cartridge is available (`1` = yes).
  - `SLOTCLOCK`: External slot clock availability  
    Indicates whether a precise 3.58MHz clock supply circuit for the cartridge is available (`1` = yes).
  - `LINEOUT`: Audio line-out availability  
    Indicates whether an output circuit for the cartridge SOUND terminal is available (`1` = yes).
  - `PSGUNIT`: PSG unit availability  
    Indicates whether the reader has PSG sound-source-equivalent functionality (`1` = yes).
  - `LFCR`: Line ending setting value  
    Displays the line ending type of the input command string (`1` means `"\n"` is treated as `0x0d,0x0a`).
  - `COMDBG`: Debug output setting via UART  
    When set to `1`, debug output is enabled on UART.
  - `SCRLOOP`: Maximum LOOP count setting for Script mode  
    Displays the maximum LOOP count for Script mode. The default value is 1000.
  - `MEMWAIT`: Wait value after Read/Write  
    Wait time after Memory Read/Write. Default value is 0.
  - `RDWAIT`: `/RD` width during Memory Read  
    Setting value for how long `/RD` remains LOW during Memory Read. Default value is 10 (180ns).
  - `WRWAIT`: `/WR` width during Memory Write  
    Setting value for how long `/WR` remains LOW during Memory Write. Default value is 10 (180ns).
  - `P6MODE`: PC-6001 cassette mode setting value  
    Displays the PC-6001 16K Read Mode setting value.

---

### 9) HSTS - Hardware Internal Status

- **Function**: Displays internal status (mainly queue remaining count and error status).
- **Format**: `HSTS`
- **Arguments**: None
- **Response**:
  - Displays the queue remaining count equivalent (`count-1`) on one line
  - `OK/FAIL`
- **Notes**:
  - Returns `FAIL` if an error occurred in the executed queue.

---

### 10) SCHK - Slot Cassette Check

- **Function**: Displays slot connection status (power ON not required).
- **Format**: `SCHK`
- **Arguments**: None
- **Response**: One of `0000/0010/0100/0110` + `OK`
- **Notes**: The response is a string in `SLOT3210` order. A slot with an inserted cartridge is shown as `1`.

---

### 11) SPON - Slot Power ON

- **Function**: Powers on the slot, enables the clock, and initializes RESET control, etc.
- **Format**: `SPON`
- **Arguments**: None
- **Response**: `OK/FAIL`
- **Notes**:
  - Returns `FAIL` if overcurrent occurs.

---

### 12) SPOFF - Slot Power OFF

- **Function**: Powers off the slot and returns signals to a safe state.
- **Format**: `SPOFF`
- **Arguments**: None
- **Response**: `OK/FAIL`
- **Notes**:

---

### 13) SRST - Slot Reset

- **Function**: Toggles the slot RESET signal to reset the slot.
- **Format**: `SRST`
- **Arguments**: None
- **Response**: `OK`

---

### 14) SSEL - Slot Select

- **Function**: Sets the default slot number.
- **Format**: `SSEL,[Slot]`
- **Arguments**:
  - `Slot` : `1` or `2`
- **Response**: `OK/FAIL`
- **Notes**:
  - Omitting `Slot` results in `FAIL`.
  - After that, this value is used whenever the slot argument is omitted.

---

### 15) SMRD - Slot Memory Read (1 byte)

- **Function**: Reads 1 byte from slot memory.
- **Format**: `SMRD,[Address](,[Slot])`
- **Arguments**:
  - `Address` : 0000〜FFFF Slot memory address
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**:
  - `<addr> : <data>` (example: `1000 : 3F`) + `OK/FAIL`

---

### 16) SMWR - Slot Memory Write (1 byte)

- **Function**: Writes 1 byte to slot memory.
- **Format**: `SMWR,[Address],[Data](,[Slot])`
- **Arguments**:
  - `Address` : 0000〜FFFF Slot memory address
  - `Data` : 00〜FF Write data
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: `OK/FAIL`

---

### 17) SMTR - Slot → Buffer Transfer Read (Bulk Read)

- **Function**: Continuously reads data from the slot into the buffer.
- **Format**: `SMTR,[Address](,[Length],[BufferAddress],[Slot])`
- **Arguments**:
  - `Address` : 0000〜FFFF Slot-side start address
  - `Length` : 0000〜FFFF Read length (maximum if omitted)
  - `BufferAddress` : 0000〜FFFF Buffer storage start position (0 if omitted)
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: Data + `OK/FAIL`

---

### 18) SMTW - Buffer → Slot Transfer Write (Bulk Write)

- **Function**: Continuously writes data from the buffer to the slot.
- **Format**: `SMTW,[Address],[Length],[BufferAddress](,[Slot])`
- **Arguments**:
  - `Address` : 0000〜FFFF Slot-side start address
  - `Length` : 0000〜FFFF Write length
  - `BufferAddress` : Buffer read start position
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: `OK/FAIL`

---

### 19) IORD - IO Read (1 byte)

- **Function**: Reads 1 byte from an IO port.
- **Format**: `IORD,[IO]`
- **Arguments**:
  - `IO` : 0000〜FFFF IO address (treated as 16-bit)
- **Response**:
  - `<io> : <data>` + `OK/FAIL`

---

### 20) IOWR - IO Write (1 byte)

- **Function**: Writes 1 byte to an IO port.
- **Format**: `IOWR,[IO],[Data]`
- **Arguments**:
  - `IO` : 0000〜FFFF IO address
  - `Data` : 00〜FF Write data
- **Response**: `OK/FAIL`

---

### 21) IOTR - IO → Buffer Transfer Read (Implementation Compliant)

- **Function**: Continuously reads from IO and stores the data into the buffer.
- **Format**: `IOTR,[IO],[Length],[BufferAddress](,[Mode])`
- **Arguments (Implementation Compliant)**:
  - `IO` : 0000〜FFFF Start IO address
  - `Length` : 0000〜FFFF Number of reads (bytes)
  - `BufferAddress` : Buffer storage start position
  - `Mode` : If set to `1`, the IO address is incremented on each access; if omitted, the same address is accessed repeatedly (feature added in V1.41 or later)
- **Response**: `OK/FAIL`

---

### 22) IOTW - Buffer → IO Transfer Write (Implementation Compliant)

- **Function**: Continuously writes data from the buffer to IO.
- **Format**: `IOTW,[IO],[Length],[BufferAddress](,[Mode])`
- **Arguments (Implementation Compliant)**:
  - `IO` : 0000〜FFFF Start IO address
  - `Length` : 0000〜FFFF Number of writes (bytes)
  - `BufferAddress` : 0000〜FFFF Buffer read start position
  - `Mode` : If set to `1`, the IO address is incremented on each access; if omitted, the same address is accessed repeatedly (feature added in V1.41 or later)
- **Response**: `OK/FAIL`

---

### 23) BDMP - Buffer Dump (Debug Use)

- **Function**: Displays buffer contents in HEX + ASCII.
- **Format**: `BDMP,[BufferAddress](,[Length])`
- **Arguments**:
  - `BufferAddress` : 0000〜FFFF Start position (0 if omitted)
  - `Length` : 0000〜FFFF Display length (128 if omitted)
- **Response**: Dump lines + `OK`

---

### 24) SDMP - Slot Dump (Slot Memory Dump)

- **Function**: Reads directly from the slot while displaying a dump.
- **Format**: `SDMP,[Address](,[Length],[Slot])`
- **Arguments**:
  - `Address` : 0000〜FFFF Start address (0 if omitted)
  - `Length` : 0000〜FFFF Display length (128 if omitted)
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: Dump lines + `OK/FAIL`

---

### 25) BSCR - Buffer Script Execute (Script Execution)

- **Function**: Executes a script stored in the buffer.
- **Format**: `BSCR,[BufferAddress](,[Slot])`
- **Arguments**:
  - `BufferAddress` : 0000〜FFFF Script start position (0 if omitted)
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: `OK/FAIL`
- **Notes**:
  - Instruction format is 4 bytes per instruction: `[cmd][addr_hi][addr_lo][data]`
  - See the separate section for script details.

---

### 26) FTEST - Factory Test

- **Function**: Executes comprehensive factory tests.
- **Format**: `FTEST`
- **Arguments**: None
- **Response**: Test logs + `OK/FAIL`

---

### 27) LEDRDY / LEDPON / LEDACC - LED Color Setting

- **Function**: Sets LED colors (READY / POWER ON / SLOT ACCESS) in RGB.
- **Format**:
  - `LEDRDY(,[R],[G],[B])`
  - `LEDPON(,[R],[G],[B])`
  - `LEDACC(,[R],[G],[B])`
- **Arguments**:
  - `R` `G` `B` : 0x00〜0xFF (optional; default values are used if omitted)
- **Response**: `OK`

---

### 28) SDBGON - Serial Debug Log ON

- **Function**: Enables debug log output from the serial port.
- **Format**: `SDBGON`
- **Arguments**: None
- **Response**: `OK`
- **Notes**:
  - Execution speed decreases because serial output increases.
  - Be careful of buffer overflows.
  - Serial output is provided through the Grove connector.

---

### 29) _FFU - Bootloader Launch

- **Function**: Switches to FFU mode and enters USB boot mode.
- **Format**: `_FFU`
- **Arguments**: None
- **Response**: Outputs a message and then reboots (no subsequent `OK/FAIL` is returned)

---

### 30) LSCR - Set Maximum LOOP Count for Script Mode

- **Function**: Sets the maximum number of loops used while waiting for a condition in Script mode.
- **Format**: `LSCR(,Maximum LOOP Count)`
- **Arguments**: 0-0xFFFF (default is 1000 if omitted)
- **Response**: `OK`

---

[Added in 260520_VER]

### 31) SMTH - Slot → Buffer Transfer Read (Bulk Read) + HASH

- **Function**: Continuously reads from the slot into the buffer and calculates a 32-bit hash code.
- **Format**: `SMTH,[Address](,[Length],[BufferAddress],[Slot])`
- **Arguments**:
  - `Address` : 0000〜FFFF Slot-side start address
  - `Length` : 0000〜FFFF Read length (maximum if omitted)
  - `BufferAddress` : 0000〜FFFF Buffer storage start position (0 if omitted)
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: `<Length> : <HASH 32bit>` + `OK/FAIL`

---

[Added in 260531_VER]

### 32) RMSET - ROM Mapper Setting

- **Function**: Sets the parameters used by the `RMRD` command.
- **Format**: `RMSET,[Mapper Selector Address],[Bank Read Address],[Bank size]`
- **Arguments**:
  - `Mapper Selector Address` : 0000〜FFFF ROM Mapper bank-switch address. If `FFFF` is specified, bank switching is not performed.
  - `Bank Read Address` : 0000〜FFFF Start address of the bank to read
  - `Bank size` : 0000〜FFFF Capacity of one bank
- **Response**: `OK/FAIL`

---

### 33) RMRD - ROM Mapper Read

- **Function**: Reads a MegaROM inserted in the slot in bulk while switching banks. (The buffer is not used.)
- **Format**: `RMRD,[Start Bank],[Bank Capacity](,[Slot])`
- **Arguments**:
  - `Start Bank` : 00〜FF Starting bank on the slot side
  - `Bank Capacity` : 00〜FF Number of banks to read
  - `Slot` : 1 or 2 (uses `defaultSlot` if omitted)
- **Response**: Read Data + `OK/FAIL`

---

## Serial Command Examples

### Example 1: Read 0x0000-0x3FFF from the slot and send it to the PC

```CMD_EX1
SPON
SMTR,0000,4000,0000,01
BSND,0000,4000
SPOFF
```

### Example 2: Receive binary data from the PC and write it to 0x8000

```CMD_EX2
SPON
BRCV,0000,2000
(Send 0x2000 bytes of binary data here)
SMTW,8000,2000,0000,01
SPOFF
```

[Added in 260531_VER]

### Example 3: Bulk-read a 1 Mbit MegaROM cassette of ASCII8K type

```CMD_EX3
SPON
RMSET,7000,8000,2000
RMRD,0,10
(Receive 0x20000 bytes of binary data here)
SPOFF
```

## Script Mode

By using a script placed in the buffer, this mode allows data read/write and comparisons
to be executed without communicating with the PC.

## Script Format

- Script format: [Command],[Address(2Byte)],[DATA]...
- Each script is fixed-length at 4 bytes.

### Script Data Format

|Address|+0 Byte|+1 Byte|+2 Byte|+3 Byte|
|---|---|---|---|---|
|**DATA**|Instruction Code|Upper Address|Lower Address|Data|

### Instruction List

|Command|Instruction Name|Description|
|---|---|---|
|0x00|NOP|Does nothing|
|0x01|Read Memory|Executes a Read access to the slot|
|0x02|Write Memory|Writes DATA to the slot (`LastData` is overwritten with DATA)|
|0x03|Read IO|Executes an IO-based Read access to the slot (`LastData` becomes the read DATA)|
|0x04|Write IO|Executes an IO-based Write access to the slot (`LastData` is overwritten with DATA)|
|0x05|Wait|Waits for the number of milliseconds specified by `address`|
|0x06|Compare|Compares `LastData` with `data`; if they match, the next instruction is skipped|
|0x07|AND|Calculates `LastData [AND] DATA`; if the result is `0x00`, the next instruction is skipped|
|0x08|OR|Calculates `LastData [OR] DATA`; if the result is `0x00`, the next instruction is skipped|
|0x09|XOR|Calculates `LastData [XOR] DATA`; if the result is `0x00`, the next instruction is skipped|
|0x0A|JMP|Skips instructions by the amount in DATA (`0x00-7F` = forward / `0x80-FF` = backward)|
|0x0B|PUSH|Writes `LastData` to the buffer memory location specified by `address`|
|0xFE|Abort|Ends script execution with failure|
|0xFF|End|Ends script execution with success|

*The JMP instruction causes script failure if it is executed at the same location a certain number of times (default: 1000).*  
*This value can be changed with the `LSCR` command.*

### Script Example

This can be executed with the following command:

```CMD_BRCV
BRCV,0,20
(Send the following binary)
BSCR,0
```

|Binary Value|Command|Description|
|---|---|---|
|0x02,0x55,0x55,0xaa|[Write Memory] Address:0x5555 Data:0xaa|Write 0xAA to 0x5555|
|0x02,0xaa,0xaa,0x55|[Write Memory] Address:0xaaaa Data:0x55|Write 0x55 to 0xAAAA|
|0x02,0x55,0x55,0xa0|[Write Memory] Address:0x5555 Data:0xa0|Write 0xA0 to 0x5555|
|0x02,0x40,0x00,0x41|[Write Memory] Address:0x4000 Data:0x41|Write 0x41 to 0x4000|
|0x01,0x40,0x00,0x00|[Read Memory] Address:0x4000|Read data from 0x4000|
|0x06,0x00,0x00,0x41|[Compare] Data:0x40|Compare with the previously read data; if it is 0x40, skip the next instruction|
|0x0a,0x00,0x00,0xfe|[JMP] -2|Return to the instruction two steps before (that is, loop until 0x4000 becomes 0x40)|
|0xff,0x00,0x00,0x00|End|End script|

## Debugging

By executing the `SDBGON` command, execution logs can be obtained from the Grove connector (UART).  
However, enabling this feature slows execution, so it should normally remain disabled.

The serial port uses 3.3V levels, and the wiring is as shown below.  
![UART](./IMAGE/uart.jpg)

### Output Example

```CMD_DEBUGOUT
BRCV_OK20
SKIP : 0a
BIN End
BSCR,0
0: Write Memory 5555:aa
4: Write Memory 2aaa:55
8: Write Memory 5555:a0
12: Write Memory 4000:41
16: Read Memory 4000:ff
20: Compare ff=41
24: JMP
16: Read Memory 4000:ff
20: Compare ff=41
24: JMP
16: Read Memory 4000:ff
20: Compare ff=41
24: JMP
16: Read Memory 4000:ff
20: Compare ff=41
24: JMP
16: Read Memory 4000:41
20: Compare 41=41
28: End Script(PASS)
```
