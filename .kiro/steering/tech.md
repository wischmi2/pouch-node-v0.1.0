# Technology Stack

## Build System & Framework

- **Primary Framework**: Zephyr RTOS (v4.1.0)
- **Build System**: CMake with Zephyr's build system
- **Package Manager**: West (Zephyr's meta-tool)
- **Module Type**: Zephyr module

## Core Dependencies

- **ZCBOR**: CBOR encoding/decoding (v0.9.1)
- **MbedTLS**: Cryptographic operations and TLS
- **LittleFS**: File system for credential storage
- **Nordic Security Backend**: For nRF Connect SDK

## Python Dependencies

```
cbor2
typer  
zcbor==0.9.1
```

Install with: `pip install -r requirements.txt`

## Supported Platforms

- **Primary**: nRF52840 (nrf52840dk/nrf52840)
- **Requirements**: Zephyr boards with BLE, PSA, MbedTLS, and LittleFS support

## Common Commands

### Building
```bash
# Build for specific board
west build -b <board_name>

# Example for nRF52840
west build -b nrf52840dk/nrf52840
```

### Code Generation
ZCBOR is used to generate CBOR encoding/decoding code from CDDL schemas:
```bash
zcbor code -c src/header.cddl -t pouch_header -sde --include-prefix cddl/ --oc header.c --oh include/cddl/header.h
```

### Provisioning (MCUmgr)
```bash
# Upload certificate
mcumgr --conntype serial --connstring $SERIAL_PORT fs upload $CERT_FILE /lfs1/credentials/crt.der

# Upload private key  
mcumgr --conntype serial --connstring $SERIAL_PORT fs upload $KEY_FILE /lfs1/credentials/key.der
```

### Testing
```bash
# Run specific test
west build -b native_sim tests/pouch/<test_name>
west build -t run
```

## Configuration

- **Kconfig**: Primary configuration system
- **Device Tree**: Hardware configuration
- **Project Config**: `prj.conf` files for application-specific settings