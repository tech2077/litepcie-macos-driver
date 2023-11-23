/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe library
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2018-2023 / EnjoyDigital  / florent@enjoy-digital.fr
 *
 */

#ifndef LITEPCIE_LIB_DMA_H
#define LITEPCIE_LIB_DMA_H

#include <stdint.h>
#include <poll.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/usb/USB.h>

#include "litepcie.h"

struct litepcie_dma_ctrl {
    uint8_t dma_channel;
    uint32_t fd;
    uint8_t use_reader, use_writer, loopback, zero_copy;
    uint8_t *buf_rd, *buf_wr;
    DMACounts* hw_counts;
    uint64_t reader_sw_count;
    uint64_t writer_sw_count;
    uint64_t reader_hw_count;
    uint64_t writer_hw_count;
    uint64_t buffers_available_read, buffers_available_write;
    uint64_t usr_read_buf_offset, usr_write_buf_offset;
};

void litepcie_dma_set_loopback(int fd, struct litepcie_dma_ctrl* dma, uint8_t loopback_enable);
void litepcie_dma_reader(struct litepcie_dma_ctrl *dma, uint8_t enable);
void litepcie_dma_writer(struct litepcie_dma_ctrl *dma, uint8_t enable);

//uint8_t litepcie_request_dma(int fd, uint8_t reader, uint8_t writer);
//void litepcie_release_dma(int fd, uint8_t reader, uint8_t writer);
//
int litepcie_dma_init(struct litepcie_dma_ctrl *dma, const char *device_name, uint8_t zero_copy);
void litepcie_dma_cleanup(struct litepcie_dma_ctrl *dma);
void litepcie_dma_process(struct litepcie_dma_ctrl *dma);
char *litepcie_dma_next_read_buffer(struct litepcie_dma_ctrl *dma);
char *litepcie_dma_next_write_buffer(struct litepcie_dma_ctrl *dma);

#endif /* LITEPCIE_LIB_DMA_H */
