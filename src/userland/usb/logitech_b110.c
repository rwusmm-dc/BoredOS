// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "logitech_b110.h"
#include "hid.h"
#include "syscall.h"
#include <string.h>
#include <stdio.h>

// Track whether the Logitech B110 driver has been initialized
static bool b110_initialized = false;

// Check if a USB device is a Logitech B110 mouse
// Returns true if the device VID:PID matches the B110
bool logitech_b110_probe(usb_device_t *dev) {
    if (!dev) return false;
    
    // Check vendor ID and product ID against known B110 values
    if (dev->vendor_id == LOGITECH_VID && dev->product_id == LOGITECH_B110_PID) {
        return true;
    }
    
    return false;
}

// Initialize the Logitech B110 mouse driver
// Sets up the HID mouse for operation
int logitech_b110_init(usb_device_t *dev) {
    if (!dev) return -1;
    
    // Use the generic HID mouse initialization
    b110_initialized = hid_init_mouse(dev);
    
    if (b110_initialized) {
        printf("[B110] Mouse USB driver initialized\n");
    }
    
    return b110_initialized ? 0 : -1;
}

// Deinitialize the Logitech B110 mouse driver
// Marks the driver as unloaded
void logitech_b110_deinit(usb_device_t *dev) {
    (void)dev;
    b110_initialized = false;
    printf("[B110] Mouse USB driver deinitialized\n");
}

// Poll the Logitech B110 mouse for input
// Reads mouse data and sends it to the window manager
int logitech_b110_poll(usb_device_t *dev) {
    if (!b110_initialized || !dev) return -1;
    
    uint8_t buffer[8];
    // Read interrupt endpoint for mouse data
    int len = usb_interrupt_in(dev, dev->endpoint_in, buffer, sizeof(buffer));
    
    if (len > 0) {
        hid_mouse_report_t report;
        hid_parse_mouse_report(buffer, len, &report);
        
        // Send mouse events to the window manager
        wm_handle_mouse(report.x, report.y, report.buttons, report.wheel);
    }
    
    return len;
}

// Driver structure for the Logitech B110 mouse
// This is registered with the driver manager
static usb_driver_t logitech_b110_driver = {
    .vendor_id = LOGITECH_VID,
    .product_id = LOGITECH_B110_PID,
    .name = "Logitech B110",
    .probe = logitech_b110_probe,
    .init = logitech_b110_init,
    .deinit = logitech_b110_deinit,
    .poll = logitech_b110_poll,
    .loaded = false
};

// Get a pointer to the Logitech B110 driver structure
// Used to register the driver with the driver manager
usb_driver_t* logitech_b110_get_driver(void) {
    return &logitech_b110_driver;
}
