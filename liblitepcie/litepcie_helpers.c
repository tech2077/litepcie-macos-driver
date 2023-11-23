/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe library
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2018-2023 / EnjoyDigital  / florent@enjoy-digital.fr
 *
 */

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>

#include <time.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "litepcie_helpers.h"
#include "litepcie.h"

int64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000U);
}

uint32_t litepcie_readl(int fd, uint32_t addr) {
    kern_return_t ret = kIOReturnSuccess;

    uint32_t olen = 1;
    uint64_t output = 0;
    uint64_t input = addr;
    
    ret = IOConnectCallScalarMethod(fd, LITEPCIE_READ_CSR, &input, 1, &output, &olen);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_READ_CSR failed with error: 0x%08x.\n", ret);
        _print_kerr_details(ret);
    }
    
    return (uint32_t)output;
}

void litepcie_writel(int fd, uint32_t addr, uint32_t val) {
    kern_return_t ret = kIOReturnSuccess;

    uint32_t olen = 0;
    uint64_t input[2] = { addr, val };
    ret = IOConnectCallScalarMethod(fd, LITEPCIE_WRITE_CSR, input, 2, NULL, &olen);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_WRITE_CSR failed with error: 0x%08x.\n", ret);
        _print_kerr_details(ret);
    }
}

void litepcie_reload(int fd) {
    kern_return_t ret = kIOReturnSuccess;
    
    size_t olen = 0;
    LitePCIeICAPCallData input = { .addr = 0x0, .data = 0x0 };
    
    ret = IOConnectCallStructMethod(fd, LITEPCIE_ICAP, &input, sizeof(LitePCIeICAPCallData), NULL, &olen);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_WRITE_CSR failed with error: 0x%08x.\n", ret);
        _print_kerr_details(ret);
    }
}

int litepcie_open(const char* name, int flags) {
    static const char* dextIdentifier = "litepcie";

    kern_return_t ret = kIOReturnSuccess;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    static io_connect_t connection = IO_OBJECT_NULL;
    
    if (connection != IO_OBJECT_NULL) {
        printf("already opened service\n");
        return connection;
    }

    /// - Tag: ClientApp_Connect
    ret = IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceNameMatching(dextIdentifier), &iterator);
    if (ret != kIOReturnSuccess) {
        printf("Unable to find service for identifier with error: 0x%08x.\n", ret);
        _print_kerr_details(ret);
    }

    printf("Searching for dext service...\n");
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        // Open a connection to this user client as a server to that client, and store the instance in "service"
        ret = IOServiceOpen(service, mach_task_self_, kIOHIDServerConnectType, &connection);

        if (ret == kIOReturnSuccess) {
            printf("\tOpened service.\n");
            break;
        } else {
            printf("\tFailed opening service with error: 0x%08x.\n", ret);
        }

        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);

    if (service == IO_OBJECT_NULL) {
        printf("Failed to match to device.\n");
        return EXIT_FAILURE;
    }
    
    return connection;
}


void litepcie_close(int fd) {
    
}

void _print_kerr_details(kern_return_t ret)
{
    printf("\tSystem: 0x%02x\n", err_get_system(ret));
    printf("\tSubsystem: 0x%03x\n", err_get_sub(ret));
    printf("\tCode: 0x%04x\n", err_get_code(ret));
}
