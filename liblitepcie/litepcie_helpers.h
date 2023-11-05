/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe library
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2018-2023 / EnjoyDigital  / florent@enjoy-digital.fr
 *
 */

#ifndef LITEPCIE_LIB_HELPERS_H
#define LITEPCIE_LIB_HELPERS_H

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>

#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>

inline void _print_kerr_details(kern_return_t ret)
{
    printf("\tSystem: 0x%02x\n", err_get_system(ret));
    printf("\tSubsystem: 0x%03x\n", err_get_sub(ret));
    printf("\tCode: 0x%04x\n", err_get_code(ret));
}

int64_t get_time_ms(void);

uint32_t litepcie_readl(int fd, uint32_t addr);
void litepcie_writel(int fd, uint32_t addr, uint32_t val);
void litepcie_reload(int fd);

#endif /* LITEPCIE_LIB_HELPERS_H */
