# ESP32

## Setup

### Install Arduino CLI
```sh
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
export PATH="$HOME/bin:$PATH"
arduino-cli version
arduino-cli config init
```

### Install ESP32 toolchain
```
# ~/.arduino15/arduino-cli.yaml
board_manager:
  additional_urls:
    - https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

```sh
arduino-cli core update-index
arduino-cli core install esp32:esp32
```
```

### Check esp32 installation
`arduino-cli core list`
```
ID          Installed Latest Name
arduino:avr 1.8.6     1.8.6  Arduino AVR Boards
esp32:esp32 3.3.1     3.3.1  esp32
```
