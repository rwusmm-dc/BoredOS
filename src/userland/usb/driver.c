// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "driver.h"
#include <string.h>
#include <stdio.h>

// Registry of available USB drivers
static usb_driver_t *driver_registry[MAX_DRIVERS];
static int driver_count = 0;

// Active driver instances (drivers loaded for specific devices)
static driver_instance_t active_instances[MAX_DRIVERS];
static int instance_count = 0;

// Initialize the driver manager
// Clears the driver registry and instance lists
void driver_manager_init(void) {
    memset(driver_registry, 0, sizeof(driver_registry));
    memset(active_instances, 0, sizeof(active_instances));
    driver_count = 0;
    instance_count = 0;
}

// Register a USB driver in the driver registry
// Returns 0 on success, -1 on failure (duplicate or full registry)
int driver_register(usb_driver_t *driver) {
    if (!driver || driver_count >= MAX_DRIVERS) return -1;
    
    // Check for duplicate driver (same VID:PID)
    for (int i = 0; i < driver_count; i++) {
        if (driver_registry[i]->vendor_id == driver->vendor_id &&
            driver_registry[i]->product_id == driver->product_id) {
            return -1;
        }
    }
    
    driver_registry[driver_count++] = driver;
    driver->loaded = false;
    return 0;
}

// Unregister a USB driver from the driver registry
// Returns 0 on success, -1 if driver not found
int driver_unregister(usb_driver_t *driver) {
    if (!driver) return -1;
    
    // Find and remove the driver from the registry
    for (int i = 0; i < driver_count; i++) {
        if (driver_registry[i] == driver) {
            // Shift remaining drivers down
            for (int j = i; j < driver_count - 1; j++) {
                driver_registry[j] = driver_registry[j + 1];
            }
            driver_count--;
            driver->loaded = false;
            return 0;
        }
    }
    
    return -1;
}

// Load and initialize a driver for a specific USB device
// Searches the registry for a matching driver and loads it
// Returns 0 on success, -1 if no matching driver found
int driver_load_for_device(usb_device_t *dev) {
    if (!dev) return -1;
    
    // Search for a driver that matches this device's VID:PID
    for (int i = 0; i < driver_count; i++) {
        usb_driver_t *driver = driver_registry[i];
        
        if (driver->vendor_id == dev->vendor_id &&
            driver->product_id == dev->product_id) {
            
            if (instance_count >= MAX_DRIVERS) return -1;
            
            // Call the driver's probe function to check if it can handle this device
            if (driver->probe && !driver->probe(dev)) {
                continue;
            }
            
            // Initialize the driver
            if (driver->init && driver->init(dev) != 0) {
                continue;
            }
            
            // Create an active instance for this device
            active_instances[instance_count].device = *dev;
            active_instances[instance_count].driver = driver;
            active_instances[instance_count].active = true;
            instance_count++;
            
            driver->loaded = true;
            printf("[DRIVER] Loaded %s for VID:PID %04X:%04X\n", 
                   driver->name, dev->vendor_id, dev->product_id);
            return 0;
        }
    }
    
    return -1;
}

// Unload a driver for a specific USB device
// Calls the driver's deinit function and removes the instance
void driver_unload_for_device(usb_device_t *dev) {
    if (!dev) return;
    
    // Find the active instance for this device
    for (int i = 0; i < instance_count; i++) {
        if (active_instances[i].active &&
            active_instances[i].device.vendor_id == dev->vendor_id &&
            active_instances[i].device.product_id == dev->product_id) {
            
            // Deinitialize the driver
            if (active_instances[i].driver->deinit) {
                active_instances[i].driver->deinit(dev);
            }
            
            active_instances[i].driver->loaded = false;
            active_instances[i].active = false;
            
            // Shift remaining instances down
            for (int j = i; j < instance_count - 1; j++) {
                active_instances[j] = active_instances[j + 1];
            }
            instance_count--;
            
            printf("[DRIVER] Unloaded driver for VID:PID %04X:%04X\n",
                   dev->vendor_id, dev->product_id);
            return;
        }
    }
}

// Check for newly connected or disconnected USB devices (hotplug)
// Loads drivers for new devices and unloads drivers for removed devices
void driver_check_hotplug(void) {
    extern int usb_get_device_count(void);
    extern usb_device_t* usb_get_device(int index);
    
    int current_count = usb_get_device_count();
    
    // Load drivers for new devices
    for (int i = 0; i < current_count; i++) {
        usb_device_t *dev = usb_get_device(i);
        if (!dev) continue;
        
        // Check if this device already has a driver loaded
        bool found = false;
        for (int j = 0; j < instance_count; j++) {
            if (active_instances[j].active &&
                active_instances[j].device.vendor_id == dev->vendor_id &&
                active_instances[j].device.product_id == dev->product_id) {
                found = true;
                break;
            }
        }
        
        // If no driver loaded, try to load one
        if (!found && !dev->initialized) {
            driver_load_for_device(dev);
            dev->initialized = true;
        }
    }
    
    // Unload drivers for removed devices
    for (int i = 0; i < instance_count; i++) {
        bool still_present = false;
        for (int j = 0; j < current_count; j++) {
            usb_device_t *dev = usb_get_device(j);
            if (dev &&
                active_instances[i].device.vendor_id == dev->vendor_id &&
                active_instances[i].device.product_id == dev->product_id) {
                still_present = true;
                break;
            }
        }
        
        if (!still_present) {
            driver_unload_for_device(&active_instances[i].device);
            i--;
        }
    }
}

// Poll all active driver instances
// Calls each driver's poll function to check for new data/events
void driver_poll_active_instances(void) {
    for (int i = 0; i < instance_count; i++) {
        if (active_instances[i].active && active_instances[i].driver->poll) {
            active_instances[i].driver->poll(&active_instances[i].device);
        }
    }
}
