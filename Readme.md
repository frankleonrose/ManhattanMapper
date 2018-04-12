[![Build Status](https://travis-ci.org/frankleonrose/ManhattanMapper.svg?branch=master)](https://travis-ci.org/frankleonrose/ManhattanMapper)

# ManhattanMapper

This is code to drive a combined TTN Mapper / MapTheThings node.

It expects to be run with the following hardware:
 - Adafruit Feather LoRa M0
 - SD Data Logger module (via SPI) for storage of GPS logs and LoRaWAN parameters.
 - Adafruit GPS FeatherWing (via Serial)
 - OLED 32x128 Display (via I2C) with 3 buttons (on dedicated pins)

# Software

The ManhattanMapper uses PlatformIO as a build tool.

1. Clone the repository to a local directory: `git clone https://github.com/frankleonrose/ManhattanMapper.git`
1. `cd` into that directory
1. Use PlatformIO (`pio`) to run unit tests locally (no device required): `pio test -e native_test`
1. Build the default environment: `pio run`
1. Upload to your device: `pio upload`

## License
Source code for ManhattanMapper is released under the MIT License,
which can be found in the [LICENSE](LICENSE) file.

## Copyright
Copyright (c) 2018 Frank Leon Rose
