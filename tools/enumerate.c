/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Lists the sensor's USB descriptors and checks which interfaces can be
 * claimed. Diagnostic only: it talks to libusb directly rather than through
 * the goodix library. */

#include <stdio.h>

#include "goodix/device.h"

static const char *transfer_type(uint8_t attributes)
{
    switch (attributes & 0x03) {
    case LIBUSB_TRANSFER_TYPE_CONTROL:     return "control";
    case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS: return "isochronous";
    case LIBUSB_TRANSFER_TYPE_BULK:        return "bulk";
    case LIBUSB_TRANSFER_TYPE_INTERRUPT:   return "interrupt";
    default:                               return "unknown";
    }
}

static void print_config(const struct libusb_config_descriptor *config)
{
    printf("active configuration: %d interface(s)\n", config->bNumInterfaces);

    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *interface = &config->interface[i];

        for (int a = 0; a < interface->num_altsetting; a++) {
            const struct libusb_interface_descriptor *desc =
                &interface->altsetting[a];

            printf("\ninterface %d (alt %d): class %d, %d endpoint(s)\n",
                   desc->bInterfaceNumber, desc->bAlternateSetting,
                   desc->bInterfaceClass, desc->bNumEndpoints);

            for (int e = 0; e < desc->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &desc->endpoint[e];

                printf("  endpoint 0x%02x  %-3s  %-11s  %u bytes\n",
                       ep->bEndpointAddress,
                       (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) ? "in" : "out",
                       transfer_type(ep->bmAttributes),
                       ep->wMaxPacketSize);
            }
        }
    }
}

static void probe_interfaces(libusb_device_handle *handle,
                             const struct libusb_config_descriptor *config)
{
    printf("\n");

    for (int i = 0; i < config->bNumInterfaces; i++) {
        int number = config->interface[i].altsetting[0].bInterfaceNumber;
        int rc     = libusb_claim_interface(handle, number);

        printf("claim interface %d: %s\n", number,
               rc == 0 ? "ok" : libusb_error_name(rc));

        if (rc == 0)
            libusb_release_interface(handle, number);
    }
}

int main(void)
{
    libusb_context                  *ctx     = NULL;
    libusb_device                  **list    = NULL;
    libusb_device                   *found   = NULL;
    libusb_device_handle            *handle  = NULL;
    struct libusb_config_descriptor *config  = NULL;
    ssize_t count;
    int     rc;
    int     status = 1;

    rc = libusb_init(&ctx);
    if (rc < 0) {
        fprintf(stderr, "libusb_init: %s\n", libusb_error_name(rc));
        return 1;
    }

    count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        fprintf(stderr, "libusb_get_device_list: %s\n",
                libusb_error_name((int)count));
        goto out;
    }

    for (ssize_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;

        if (libusb_get_device_descriptor(list[i], &desc) == 0 &&
            desc.idVendor == GOODIX_VID && desc.idProduct == GOODIX_PID) {
            found = list[i];
            break;
        }
    }

    if (found == NULL) {
        fprintf(stderr, "device %04x:%04x not found\n", GOODIX_VID, GOODIX_PID);
        goto out;
    }

    printf("found %04x:%04x on bus %d, address %d\n\n",
           GOODIX_VID, GOODIX_PID,
           libusb_get_bus_number(found), libusb_get_device_address(found));

    rc = libusb_get_active_config_descriptor(found, &config);
    if (rc < 0) {
        fprintf(stderr, "libusb_get_active_config_descriptor: %s\n",
                libusb_error_name(rc));
        goto out;
    }

    print_config(config);

    rc = libusb_open(found, &handle);
    if (rc < 0) {
        fprintf(stderr, "\nlibusb_open: %s\n", libusb_error_name(rc));
        goto out;
    }

    probe_interfaces(handle, config);
    status = 0;

out:
    if (handle)
        libusb_close(handle);
    if (config)
        libusb_free_config_descriptor(config);
    if (list)
        libusb_free_device_list(list, 1);
    libusb_exit(ctx);

    return status;
}
