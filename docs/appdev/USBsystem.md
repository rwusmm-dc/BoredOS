# BoredOS USB System Documentation

## Overview

The BoredOS USB system provides a modular, extensible framework for USB device support in the kernel. The implementation follows a layered architecture with clear separation between host controller management, device enumeration, and driver management. The system currently supports UHCI (Universal Host Controller Interface) with infrastructure in place for OHCI, EHCI, and XHCI expansion.

## Architecture

### Core Components

The USB system consists of several key components that work together to provide USB functionality:

- **Host Controller Layer**: Manages USB host controllers (UHCI/OHCI/EHCI/XHCI)
- **Device Enumeration Layer**: Handles device discovery and configuration
- **Driver Management Layer**: Provides plug-and-play driver loading/unloading
- **Transfer Layer**: Implements USB transfer types (control, interrupt, bulk, isochronous)

### Directory Structure

```
src/userland/usb/
├── usb.c              # Core USB stack implementation
├── usb.h              # USB core data structures and constants
├── uhci.c             # UHCI host controller driver
├── uhci.h             # UHCI-specific definitions
├── driver.c           # Driver management system
├── driver.h           # Driver framework definitions
├── hid.h              # HID class definitions
├── logitech_b110.c    # Example device driver (Logitech B110 mouse)
├── logitech_b110.h    # Logitech B110 driver definitions
└── syscall.h          # System call interface definitions
```

## Host Controller Support

### UHCI Implementation

UHCI (Universal Host Controller Interface) is the primary supported host controller. The implementation includes:

- **Frame List Management**: 1024-entry frame list for transfer scheduling
- **Transfer Descriptors**: TD pools for efficient memory management
- **Queue Heads**: QH management for transfer chaining
- **Port Management**: Port reset, enable/disable, and status monitoring

Key UHCI features:
- Memory-mapped I/O access via PCI BAR0
- Interrupt and control transfer support
- Automatic port detection and device reset
- Frame-based scheduling (1ms frames)

### Controller Detection

The system automatically detects USB host controllers during initialization by scanning the PCI bus:

```c
// Class code 0x0C indicates USB controller
// Programming interface determines controller type:
// 0x00 = UHCI, 0x10 = OHCI, 0x20 = EHCI, 0x30 = XHCI
```

## Device Enumeration

### Enumeration Process

1. **Controller Initialization**: Initialize detected host controllers
2. **Port Scanning**: Check each controller's ports for connected devices
3. **Device Reset**: Reset newly detected devices
4. **Descriptor Reading**: Read device and configuration descriptors
5. **Address Assignment**: Assign unique USB addresses to devices
6. **Driver Loading**: Load appropriate drivers based on VID:PID

### Device Structure

Each USB device is represented by the `usb_device_t` structure:

```c
typedef struct {
    uint16_t vendor_id;           // Vendor identifier
    uint16_t product_id;          // Product identifier
    uint8_t device_class;         // USB device class
    uint8_t device_subclass;      // Device subclass
    uint8_t device_protocol;      // Device protocol
    uint8_t config_value;         // Configuration value
    uint8_t interface_number;     // Interface number
    uint8_t alternate_setting;    // Alternate setting
    uint8_t endpoint_in;          // IN endpoint address
    uint8_t endpoint_out;         // OUT endpoint address
    uint16_t max_packet_size;     // Maximum packet size
    bool initialized;            // Initialization status
} usb_device_t;
```

## Driver Management System

### Driver Registration

Drivers are registered with the driver manager using the `usb_driver_t` structure:

```c
typedef struct {
    uint16_t vendor_id;                    // Target vendor ID
    uint16_t product_id;                   // Target product ID
    char name[64];                         // Driver name
    bool (*probe)(usb_device_t *dev);     // Device probing function
    int (*init)(usb_device_t *dev);       // Initialization function
    void (*deinit)(usb_device_t *dev);    // Deinitialization function
    int (*poll)(usb_device_t *dev);       // Polling function
    bool loaded;                          // Load status
} usb_driver_t;
```

### Hotplug Support

The system supports hotplug detection and automatic driver loading:

- **Device Detection**: Monitors for newly connected devices
- **Driver Matching**: Matches devices to registered drivers by VID:PID
- **Automatic Loading**: Loads appropriate drivers for new devices
- **Cleanup**: Unloads drivers when devices are disconnected

### Driver Lifecycle

1. **Registration**: Driver registers with the system
2. **Probing**: System probes driver for device compatibility
3. **Initialization**: Driver initializes compatible devices
4. **Operation**: Driver handles device I/O through polling
5. **Deinitialization**: Driver cleanup on device removal

## Transfer Types

### Control Transfers

Control transfers are used for device configuration and status requests:

```c
int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, void *data);
```

The UHCI implementation handles control transfers through a three-stage TD chain:
- **Setup Stage**: Sends the 8-byte setup packet
- **Data Stage**: Optional data phase (IN or OUT)
- **Status Stage**: Transfer completion handshake

### Interrupt Transfers

Interrupt transfers are used for periodic data such as HID input:

```c
int usb_interrupt_in(usb_device_t *dev, uint8_t endpoint, void *data, uint16_t len);
```

Features:
- Periodic scheduling (typically 8ms for HID devices)
- Low-latency data delivery
- Automatic retry on errors

## Adding New USB Drivers

### Driver Template

To add a new USB driver, create a source file following this template:

```c
// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.

#include "driver.h"
#include "usb.h"
#include <stdio.h>
#include <string.h>

// Driver-specific constants
#define VENDOR_ID    0x1234
#define PRODUCT_ID   0x5678

// Driver state
static bool driver_initialized = false;

// Probe function - check if driver can handle this device
bool mydevice_probe(usb_device_t *dev) {
    if (!dev) return false;
    
    // Check VID:PID match
    if (dev->vendor_id == VENDOR_ID && dev->product_id == PRODUCT_ID) {
        return true;
    }
    
    return false;
}

// Initialize the device
int mydevice_init(usb_device_t *dev) {
    if (!dev) return -1;
    
    // Perform device-specific initialization
    // Set up endpoints, configure device, etc.
    
    driver_initialized = true;
    printf("[MYDEVICE] Driver initialized\n");
    return 0;
}

// Deinitialize the device
void mydevice_deinit(usb_device_t *dev) {
    (void)dev;
    driver_initialized = false;
    printf("[MYDEVICE] Driver deinitialized\n");
}

// Poll the device for data/events
int mydevice_poll(usb_device_t *dev) {
    if (!driver_initialized || !dev) return -1;
    
    // Read data from device
    uint8_t buffer[64];
    int len = usb_interrupt_in(dev, dev->endpoint_in, buffer, sizeof(buffer));
    
    if (len > 0) {
        // Process received data
        // Handle device-specific protocol
    }
    
    return len;
}

// Driver structure
static usb_driver_t mydevice_driver = {
    .vendor_id = VENDOR_ID,
    .product_id = PRODUCT_ID,
    .name = "My USB Device",
    .probe = mydevice_probe,
    .init = mydevice_init,
    .deinit = mydevice_deinit,
    .poll = mydevice_poll,
    .loaded = false
};

// Driver entry point
usb_driver_t* mydevice_get_driver(void) {
    return &mydevice_driver;
}
```

### Integration Steps

1. **Create Driver Files**: Create `.c` and `.h` files for your driver
2. **Implement Required Functions**: Implement probe, init, deinit, and poll functions
3. **Register Driver**: Call `driver_register()` during system initialization
4. **Add to Build**: Include your driver files in the Makefile
5. **Test**: Test with actual hardware or USB passthrough

### Best Practices

- **Error Handling**: Always check return values and handle errors gracefully
- **Resource Management**: Clean up resources in deinit function
- **Thread Safety**: Use appropriate synchronization if accessing shared data
- **Performance**: Minimize polling frequency to reduce CPU usage
- **Standards Compliance**: Follow USB specification for your device class

## HID Support

The USB system includes basic HID (Human Interface Device) support:

### HID Classes Supported

- **HID Boot Mouse**: Basic mouse functionality
- **HID Boot Keyboard**: Basic keyboard functionality (planned)

### HID Report Processing

HID reports are parsed through the generic HID layer:

```c
// Parse mouse report
hid_mouse_report_t report;
hid_parse_mouse_report(buffer, len, &report);

// Handle mouse events
wm_handle_mouse(report.x, report.y, report.buttons, report.wheel);
```

## Limitations and Future Development

### Current Limitations

- **UHCI Only**: Only UHCI host controllers are fully supported
- **Basic HID**: Limited HID class support
- **No USB 3.0**: XHCI (USB 3.0) support not implemented
- **Limited Transfer Types**: Only control and interrupt transfers supported
- **No Power Management**: No USB power management features

### Planned Enhancements

- **OHCI/EHCI Support**: Add support for older host controllers
- **XHCI Support**: Implement USB 3.0 host controller support
- **Mass Storage**: Add USB mass storage class support
- **Audio Support**: Implement USB audio class
- **Hub Support**: Add USB hub enumeration and management
- **Power Management**: Implement USB power management features

## Debugging USB Issues

### Common Issues

1. **Device Not Detected**: Check PCI configuration and controller initialization
2. **Driver Not Loading**: Verify VID:PID matching and driver registration
3. **Transfer Failures**: Check endpoint configuration and TD setup
4. **Enumeration Failures**: Verify device reset and descriptor reading

### Debug Techniques

- **Serial Output**: Use serial_write() for debugging output
- **PCI Analysis**: Check PCI configuration space for controller detection
- **Transfer Monitoring**: Monitor transfer descriptor status fields
- **Driver Logging**: Add logging to driver functions for troubleshooting

## API Reference

### Core Functions

```c
// Initialize USB stack
void usb_init(void);

// Detect controller type
usb_hc_type_t usb_detect_controller(usb_hc_t *hc);

// Enumerate a device
bool usb_enumerate_device(usb_device_t *dev);

// Perform control transfer
int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, void *data);

// Perform interrupt transfer
int usb_interrupt_in(usb_device_t *dev, uint8_t endpoint, void *data, uint16_t len);

// Enumerate all devices
void usb_enumerate_devices(void);

// Get device count
int usb_get_device_count(void);

// Get device by index
usb_device_t* usb_get_device(int index);
```

### Driver Management Functions

```c
// Initialize driver manager
void driver_manager_init(void);

// Register a driver
int driver_register(usb_driver_t *driver);

// Unregister a driver
int driver_unregister(usb_driver_t *driver);

// Load driver for device
int driver_load_for_device(usb_device_t *dev);

// Unload driver for device
void driver_unload_for_device(usb_device_t *dev);

// Check for hotplug events
void driver_check_hotplug(void);

// Poll active drivers
void driver_poll_active_instances(void);
```

## Conclusion

The BoredOS USB system provides a solid foundation for USB device support with room for expansion. The modular design allows for easy addition of new device drivers and host controller support. While currently focused on basic HID devices through UHCI, the architecture supports future expansion to include USB 3.0, mass storage, and other device classes.

For developers looking to contribute or add USB support for their devices, the driver framework provides a clean, well-defined interface that follows established USB development practices.