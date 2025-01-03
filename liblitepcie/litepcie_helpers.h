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
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/usb/USB.h>

#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

void _print_kerr_details(kern_return_t ret);

int64_t get_time_ms(void);

uint32_t litepcie_readl(int fd, uint32_t addr);
void litepcie_writel(int fd, uint32_t addr, uint32_t val);
void litepcie_reload(int fd);

int litepcie_open(const char* name, int flags);

void litepcie_close(int fd);

#endif /* LITEPCIE_LIB_HELPERS_H */
