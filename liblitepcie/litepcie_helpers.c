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

    uint32_t olen = 0;
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
