// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it has in it, as per the GPL license terms.
#include "usb.h"
#include "uhci.h"
#include "syscall.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Array of USB host controllers (max 8 supported)
static usb_hc_t usb_controllers[8];
static int usb_controller_count = 0;

// Array of detected USB devices
static usb_device_t usb_devices[USB_MAX_DEVICES];
static int usb_device_count = 0;

// Counter for assigning unique addresses to USB devices
static uint8_t device_address_counter = 1;

// Detect the type of USB host controller by reading PCI configuration space
// Returns the controller type (UHCI/OHCI/EHCI/XHCI) or UNKNOWN if not recognized
usb_hc_type_t usb_detect_controller(usb_hc_t *hc) {
    // Read class code from PCI config register 0x08 (byte 3)
    uint32_t class_code = pci_read_config(hc->bus, hc->device, hc->function, 0x08) >> 24;
    // Read programming interface from PCI config register 0x09
    uint32_t prog_if = pci_read_config(hc->bus, hc->device, hc->function, 0x09) & 0xFF;
    
    // Class code 0x0C indicates a USB controller
    if (class_code == 0x0C) {
        if (prog_if == 0x00) return USB_HC_UHCI;
        if (prog_if == 0x10) return USB_HC_OHCI;
        if (prog_if == 0x20) return USB_HC_EHCI;
        if (prog_if == 0x30) return USB_HC_XHCI;
    }
    
    return USB_HC_UNKNOWN;
}

// Initialize the USB stack by scanning PCI bus for USB host controllers
// Currently only UHCI is supported for initialization
void usb_init(void) {
    printf("[USB] Initializing USB stack...\n");
    
    int idx = 0;
    
    // Scan entire PCI bus space for USB controllers
    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            for (int function = 0; function < 8; function++) {
                // Check if device exists by reading vendor ID
                uint32_t vendor_id = pci_read_config(bus, device, function, 0x00) & 0xFFFF;
                if (vendor_id == 0xFFFF) continue;
                
                uint32_t class_code = pci_read_config(bus, device, function, 0x08) >> 24;
                
                // If this is a USB controller, save its info
                if (class_code == 0x0C && idx < 8) {
                    usb_controllers[idx].bus = bus;
                    usb_controllers[idx].device = device;
                    usb_controllers[idx].function = function;
                    // Read base address registers (BARs) for memory-mapped I/O
                    usb_controllers[idx].bar0 = pci_read_config(bus, device, function, 0x10);
                    usb_controllers[idx].bar1 = pci_read_config(bus, device, function, 0x14);
                    usb_controllers[idx].bar2 = pci_read_config(bus, device, function, 0x18);
                    usb_controllers[idx].bar3 = pci_read_config(bus, device, function, 0x1C);
                    usb_controllers[idx].bar4 = pci_read_config(bus, device, function, 0x20);
                    
                    usb_hc_type_t type = usb_detect_controller(&usb_controllers[idx]);
                    const char *type_str = "UNKNOWN";
                    if (type == USB_HC_UHCI) type_str = "UHCI";
                    else if (type == USB_HC_OHCI) type_str = "OHCI";
                    else if (type == USB_HC_EHCI) type_str = "EHCI";
                    else if (type == USB_HC_XHCI) type_str = "XHCI";
                    
                    printf("[USB] Found controller: %s\n", type_str);
                    
                    // Initialize the controller if we support it
                    if (type == USB_HC_UHCI) {
                        if (uhci_init(&usb_controllers[idx])) {
                            printf("[USB] UHCI initialized\n");
                        }
                    }
                    
                    idx++;
                }
            }
        }
    }
    
    usb_controller_count = idx;
    printf("[USB] Total controllers found: %d\n", usb_controller_count);
}

// Enumerate a USB device by reading its device descriptor and assigning it an address
// Returns true if enumeration succeeded, false otherwise
bool usb_enumerate_device(usb_device_t *dev) {
    if (usb_device_count >= USB_MAX_DEVICES) return false;
    
    usb_setup_packet_t setup;
    uint8_t desc[18]; // Device descriptor is 18 bytes
    
    // Request the device descriptor to identify the device
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 18;
    
    if (usb_control_transfer(dev, &setup, desc) < 0) {
        return false;
    }
    
    // Parse the device descriptor
    dev->vendor_id = desc[8] | (desc[9] << 8);
    dev->product_id = desc[10] | (desc[11] << 8);
    dev->device_class = desc[4];
    dev->device_subclass = desc[5];
    dev->device_protocol = desc[6];
    dev->max_packet_size = desc[7];
    
    // Assign a unique USB address to the device
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = device_address_counter;
    setup.wIndex = 0;
    setup.wLength = 0;
    
    if (usb_control_transfer(dev, &setup, NULL) < 0) {
        return false;
    }
    
    // Set default configuration values (simplified for basic devices)
    dev->config_value = 1;
    dev->interface_number = 0;
    dev->endpoint_in = 0x81; // Interrupt IN endpoint
    dev->endpoint_out = 0x01; // Interrupt OUT endpoint
    
    usb_devices[usb_device_count++] = *dev;
    device_address_counter++;
    
    return true;
}

// Perform a USB control transfer
// Currently assumes only one controller is present
int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, void *data) {
    (void)dev;
    if (usb_controller_count == 0) return -1;
    
    usb_hc_t *hc = &usb_controllers[0];
    uint16_t len = setup->wLength;
    
    return uhci_control_transfer(hc, setup, data, len);
}

// Perform a USB interrupt IN transfer (used for things like mouse/keyboard data)
int usb_interrupt_in(usb_device_t *dev, uint8_t endpoint, void *data, uint16_t len) {
    (void)dev;
    if (usb_controller_count == 0) return -1;
    
    usb_hc_t *hc = &usb_controllers[0];
    
    return uhci_interrupt_in(hc, endpoint, data, len);
}

// Enumerate devices on all USB controller ports
// Currently checks 2 ports per controller
void usb_enumerate_devices(void) {
    printf("[USB] Starting device enumeration\n");
    
    if (usb_controller_count == 0) {
        printf("[USB] No controllers available\n");
        return;
    }
    
    // Try to enumerate devices on ports 1 and 2
    for (int port = 1; port <= 2; port++) {
        usb_device_t dev;
        memset(&dev, 0, sizeof(usb_device_t));
        
        if (usb_enumerate_device(&dev)) {
            printf("[USB] Device enumerated: VID=%04X PID=%04X\n", dev.vendor_id, dev.product_id);
        }
    }
}

// Get the total number of enumerated USB devices
int usb_get_device_count(void) {
    return usb_device_count;
}

// Get a pointer to a USB device by its index
usb_device_t* usb_get_device(int index) {
    if (index < 0 || index >= usb_device_count) return NULL;
    return &usb_devices[index];
}
