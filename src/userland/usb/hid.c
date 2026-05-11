// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "hid.h"
#include "logitech_b110.h"
#include <string.h>
#include <stdio.h>

// Array of HID mouse devices that have been initialized
static usb_device_t hid_mouse_devices[8];
static int hid_mouse_count = 0;

// Parse a HID report descriptor for a mouse device
// Extracts information like button count, axis ranges, and wheel support
bool hid_parse_mouse_descriptor(const uint8_t *desc, int len, hid_mouse_desc_t *out) {
    if (!desc || !out || len < 10) return false;
    
    memset(out, 0, sizeof(hid_mouse_desc_t));
    
    // Parse HID descriptor byte by byte
    int i = 0;
    while (i < len) {
        uint8_t prefix = desc[i++];
        uint8_t size = prefix & 0x03;  // Data size (0=0, 1=1, 2=2, 3=4 bytes)
        uint8_t type = (prefix >> 2) & 0x03;  // Item type (0=Main, 1=Global, 2=Local, 3=Reserved)
        uint8_t tag = prefix >> 4;  // Item tag
        
        // Read the data value based on size
        uint32_t value = 0;
        if (size == 1) {
            value = desc[i++];
        } else if (size == 2) {
            value = desc[i];
            i++;
            value |= (desc[i] << 8);
            i++;
        } else if (size == 3) {
            value = desc[i];
            i++;
            value |= (desc[i] << 8);
            i++;
            value |= (desc[i] << 16);
            i++;
            value |= (desc[i] << 24);
            i++;
        }
        
        // Global items describe usage pages and usage values
        if (type == 1) {
            if (tag == 0x04) out->usage_page = value;  // Usage Page
            if (tag == 0x08) out->usage = value;  // Usage
        }
        
        // Local items describe specific capabilities
        if (type == 2) {
            if (tag == 0x01 && value == 0x30) out->max_x = 32767;  // X-axis usage
            if (tag == 0x01 && value == 0x31) out->max_y = 32767;  // Y-axis usage
            if (tag == 0x09 && value == 0x38) out->has_wheel = true;  // Wheel usage
        }
    }
    
    // Set defaults for standard mouse
    out->button_count = 3;
    out->usage_page = 0x01;  // Generic Desktop
    out->usage = 0x02;  // Mouse
    
    return true;
}

// Initialize a HID mouse device
// Sets the mouse to boot protocol and configures it for operation
bool hid_init_mouse(usb_device_t *dev) {
    if (hid_mouse_count >= 8) return false;
    
    usb_setup_packet_t setup;
    
    // Set the mouse to boot protocol (simplified HID mode)
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = USB_REQ_SET_PROTOCOL;
    setup.wValue = 0;  // Boot protocol
    setup.wIndex = dev->interface_number;
    setup.wLength = 0;
    
    usb_control_transfer(dev, &setup, NULL);
    
    // Set idle rate to 0 (send reports immediately on change)
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = USB_REQ_SET_IDLE;
    setup.wValue = 0;
    setup.wIndex = dev->interface_number;
    setup.wLength = 0;
    
    usb_control_transfer(dev, &setup, NULL);
    
    hid_mouse_devices[hid_mouse_count++] = *dev;
    dev->initialized = true;
    
    printf("[HID] Mouse initialized\n");
    return true;
}

// Get a mouse input report from the device
// Reads the interrupt endpoint and parses button/movement data
int hid_mouse_get_report(usb_device_t *dev, hid_mouse_report_t *report) {
    if (!dev || !report) return -1;
    
    uint8_t buffer[8];
    int len = usb_interrupt_in(dev, dev->endpoint_in, buffer, sizeof(buffer));
    
    if (len < 3) return -1;
    
    // Parse standard mouse boot protocol report:
    // Byte 0: button states
    // Byte 1: X movement (signed)
    // Byte 2: Y movement (signed)
    // Byte 3: wheel movement (signed, optional)
    report->buttons = buffer[0];
    report->x = (int8_t)buffer[1];
    report->y = (int8_t)buffer[2];
    report->wheel = (len >= 4) ? (int8_t)buffer[3] : 0;
    
    return len;
}

// Initialize the HID subsystem and scan for HID devices
// Currently only handles mouse devices
void usb_hid_init(void) {
    printf("[HID] Initializing HID subsystem\n");
    
    int device_count = usb_get_device_count();
    
    // Scan all USB devices for HID class devices
    for (int i = 0; i < device_count; i++) {
        usb_device_t *dev = usb_get_device(i);
        
        if (dev && dev->device_class == USB_CLASS_HID) {
            printf("[HID] Found HID device\n");
            
            // Check if this is a Logitech B110 mouse and initialize it
            if (logitech_b110_probe(dev)) {
                printf("[HID] Initializing Logitech B110\n");
                logitech_b110_init(dev);
            }
        }
    }
}
