# Product Overview

**Pouch** is a non-IP protocol for communication between devices and cloud services, typically through one or more gateways. It enables devices not directly connected to the internet to communicate with Golioth Cloud services.

## Key Features

- **Transport Agnostic**: Currently supports BLE GATT transport with extensible architecture for additional transports
- **Secure Communication**: Implements streaming AEAD encryption (ChaCha20-Poly1305 or AES-GCM) with certificate-based authentication
- **Golioth Integration**: Built-in support for Golioth device management services including logging, OTA updates, settings, and streaming
- **Zephyr Module**: Designed as a Zephyr RTOS module for embedded systems

## Current Status

⚠️ **Under Active Development** - Breaking changes may be introduced at any time.

## Supported Golioth Services

- ✅ Logging
- ✅ OTA (Over-the-Air Updates)  
- ✅ Settings
- ✅ Stream
- 🚧 Remote Procedure Calls (RPCs) - In development
- 🚧 State - In development