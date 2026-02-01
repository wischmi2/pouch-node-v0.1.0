# Project Structure

## Root Directory Layout

```
pouch/
├── src/                    # Core Pouch protocol implementation
├── include/                # Public API headers
├── golioth_sdk/           # Golioth service implementations
├── lib/                   # Reusable libraries
├── examples/              # Example applications
├── tests/                 # Test suites
├── cert2/                 # Certificate files
└── zephyr/                # Zephyr module configuration
```

## Core Implementation (`src/`)

- **Protocol Core**: `pouch.c`, `uplink.c`, `downlink.c`, `header.c`
- **Data Handling**: `buf.c`, `block.c`, `entry.c`, `stream.c`
- **Security**: `crypto_*.c`, `cert.c`, `saead/` directory
- **Transport Layer**: `transport/` directory (currently BLE GATT)

## Public API (`include/`)

- `include/pouch/` - Main Pouch API
- `include/golioth/` - Golioth service APIs

## Libraries (`lib/`)

- `pouch_ble_gatt_common/` - Shared BLE GATT utilities
- `zcbor_utils/` - CBOR utility functions

## Examples (`examples/`)

- `ble_gatt/` - Complete BLE GATT example with provisioning

## Tests (`tests/`)

- `tests/pouch/` - Unit and integration tests
- Organized by functionality: `downlink/`, `uplink/`, `encryption/`

## Configuration Files

- `CMakeLists.txt` - Main build configuration
- `Kconfig` - Configuration options
- `west.yml` - West manifest for dependencies
- `requirements.txt` - Python dependencies

## Naming Conventions

- **Files**: Snake_case (e.g., `ble_gatt_peripheral.c`)
- **Directories**: Snake_case (e.g., `ble_gatt/`)
- **Headers**: Match source file names
- **Config Options**: `CONFIG_POUCH_*` prefix

## Linker Scripts

- `*.ld` files define memory sections for handlers
- `downlink_handlers.ld` - Downlink message handlers
- `event_handlers.ld` - Event handlers
- `attributes.ld` - BLE GATT attributes

## Generated Files

- CBOR encode/decode files generated from `*.cddl` schemas
- Certificate includes generated from DER files
- Build artifacts in `build/` directory (not tracked)