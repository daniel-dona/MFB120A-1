# MFB120A-1
Xiaomi Smart Multifunctional Rice Cooker custom firmware

# Hardware I/O

- 2 temperature inputs
- 1 relay controlled heat source
- 8 single color LED (modes + keep warm)
- 1 dual color LED (WiFi)
- 4 digit 7 segment display with middle 2 dots
- 1 beeper
- 4 buttons

# MCU / ESP32

An ESP32 is used as the main SoC, controlling indirectly all the inputs and outputs and also providing conectivity. An additional unidentified chip (MCU) works as a GPIO extension, controlling the hardware and comunicating using UART with the ESP32. 

Data packages from ESP32 to MCU are 11 bytes length, including 1 byte header (0x55) and 2 bytes CRC16/XMODEM:

```
{0x55,0x07,0x10,0xff,0xff,0xff,0xff,0x1f,0x1f,0x8a,0x20}
```

Every package is answered by the MCU with what looks like an status report of the inputs, 10 bytes length, including 1 byte header (0xaa) and the same CRC:

```
{0xaa,0x06,0x80,0x14,0x14,0x00,0x30,0x00,0x2c,0x9a}
```

## UART format: ESP32 to MCU

Known usage, left to right in the byte array of the package.

### Byte 0
Constant header

### Byte 1
Could be part of the header (?)

### Byte 2
Seems to be multiple misc functions and some actuators, some bit sets the ESP32 to reset/off (?)

- Bit 4: beep
- 

### Byte 3 to 6
Numbers, only 4 and 5 have a dot (time separation dots), all follow the same schema for representing the 7 segmens

```c
	uint8_t table [] = { 
		0b00111111, // 0
		0b00000110, // 1
		0b01011011, // 2
		0b01001111, // 3
		0b01100110, // 4
		0b01101101, // 5
		0b01111101, // 6
		0b00000111, // 7
		0b01111111, // 8
		0b01101111, // 9
	};
```

Remaining bit it the dot when available

### Byte 7
LEDS 1 to 5

### Byte 8
LEDS 6 to 9, 9 uses 2 bits for both colors

### Bytes 9 and 10
CRC16 / XMODEM of the bytes 1 to 8.

## UART format: ESP32 to MCU

### Byte 0
Constant header

### Byte 1
Could be part of the header (?)

### Byte 2
Top sensor temperature (looks like is simply the int of ºC)

### Byte 3
Bottom sensor temperature (looks like is simply the int of ºC)

[...]

### Bytes 8 and 9
CRC16 / XMODEM of the bytes 1 to 7.


# Build

At the repo root folder:

```
docker run --rm -v "${PWD}":/config -it ghcr.io/esphome/esphome compile rice.yaml
```

# Flash
