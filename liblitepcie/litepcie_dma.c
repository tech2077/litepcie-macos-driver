/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LitePCIe library
 *
 * This file is part of LitePCIe.
 *
 * Copyright (C) 2018-2023 / EnjoyDigital  / florent@enjoy-digital.fr
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "litepcie_dma.h"
#include "litepcie_helpers.h"
#include "litepcie.h"


void litepcie_dma_set_loopback(int fd, struct litepcie_dma_ctrl *dma, uint8_t loopback_enable) {
    printf("litepcie_dma_set_loopback\n");
#ifdef CSR_PCIE_DMA0_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 0) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA0_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA1_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 1) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA1_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA2_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 2) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA2_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA3_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 3) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA3_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA4_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 4) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA4_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA5_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 5) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA5_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA6_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 6) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA6_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA7_LOOPBACK_ENABLE_ADDR
    if (dma->dma_channel == 7) {
        litepcie_writel(fd, CSR_TO_OFFSET(CSR_PCIE_DMA7_LOOPBACK_ENABLE_ADDR), loopback_enable ? 1 : 0);
    }
#endif
}

void litepcie_dma_writer(struct litepcie_dma_ctrl *dma, uint8_t enable) {
    kern_return_t ret = kIOReturnSuccess;
    
    LitePCIeConfigDmaChannelData data;
    data.channel = dma->dma_channel;
    data.enable = enable;
    
    ret = IOConnectCallStructMethod(dma->fd, LITEPCIE_CONFIG_DMA_WRITER_CHANNEL, &data, sizeof(LitePCIeConfigDmaChannelData), NULL, 0);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_CONFIG_DMA_WRITER_CHANNEL failed with error: 0x%08x.\n", ret);
        _print_kerr_details(ret);
    }
    
    dma->writer_enabled = enable;
}

void litepcie_dma_reader(struct litepcie_dma_ctrl *dma, uint8_t enable) {
    kern_return_t ret = kIOReturnSuccess;
    
    LitePCIeConfigDmaChannelData data;
    data.channel = dma->dma_channel;
    data.enable = enable;
    
    ret = IOConnectCallStructMethod(dma->fd, LITEPCIE_CONFIG_DMA_READER_CHANNEL, &data, sizeof(LitePCIeConfigDmaChannelData), NULL, 0);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_CONFIG_DMA_READER_CHANNEL failed with error: 0x%08x.\n", ret);
        _print_kerr_details(ret);
    }
    
    dma->reader_enabled = enable;
}

///* lock */
//
//uint8_t litepcie_request_dma(int fd, uint8_t reader, uint8_t writer) {
////    struct litepcie_ioctl_lock m;
////    m.dma_reader_request = reader > 0;
////    m.dma_writer_request = writer > 0;
////    m.dma_reader_release = 0;
////    m.dma_writer_release = 0;
////    checked_ioctl(fd, LITEPCIE_IOCTL_LOCK, &m);
////    return m.dma_reader_status;
//    return 0;
//}
//
//void litepcie_release_dma(int fd, uint8_t reader, uint8_t writer) {
////    struct litepcie_ioctl_lock m;
////    m.dma_reader_request = 0;
////    m.dma_writer_request = 0;
////    m.dma_reader_release = reader > 0;
////    m.dma_writer_release = writer > 0;
////    checked_ioctl(fd, LITEPCIE_IOCTL_LOCK, &m);
//}

int litepcie_dma_init(struct litepcie_dma_ctrl *dma, const char *device_name, uint8_t zero_copy)
{
    kern_return_t ret = kIOReturnSuccess;
    
    dma->reader_sw_count = 0;
    dma->writer_sw_count = 0;

    dma->zero_copy = zero_copy;

    dma->fd = litepcie_open(device_name, O_RDWR);

    /* request dma reader and writer */
//    if ((litepcie_request_dma(dma->fds.fd, dma->use_reader, dma->use_writer) == 0)) {
//        fprintf(stderr, "DMA not available\n");
//        return -1;
//    }

    litepcie_dma_set_loopback(dma->fd, dma, dma->loopback);
    
    if (dma->use_writer) {
        mach_vm_address_t writerAddress = 0;
        mach_vm_size_t writerSize = 0;
        
        ret = IOConnectMapMemory64(dma->fd, LITEPCIE_DMA_WRITER | 0, mach_task_self(), &writerAddress, &writerSize, kIOMapAnywhere);
        
        printf("writer ret: 0x%x\n", ret);
        
        if (writerAddress != 0) {
            dma->buf_rd = (uint8_t*)writerAddress;
        } else {
            printf("failed to acquire writer mapped buffer");
            return EXIT_FAILURE;
        }
    }
    
    if (dma->use_reader) {
        mach_vm_address_t readerAddress = 0;
        mach_vm_size_t readerSize = 0;
        ret = IOConnectMapMemory64(dma->fd, LITEPCIE_DMA_READER | 0, mach_task_self(), &readerAddress, &readerSize, kIOMapAnywhere);
        
        printf("reader ret: 0x%x\n", ret);
        
        if (readerAddress != 0) {
            dma->buf_wr = (uint8_t*)readerAddress;
        } else {
            printf("failed to acquire reader mapped buffer");
            return EXIT_FAILURE;
        }
    }
    
    if (dma->use_writer || dma->use_reader) {
        mach_vm_address_t countAddress = 0;
        mach_vm_size_t countsize = 0;
        ret = IOConnectMapMemory64(dma->fd, LITEPCIE_DMA_COUNTS | 0, mach_task_self(), &countAddress, &countsize, kIOMapAnywhere);
        
        printf("counts ret: 0x%x\n", ret);
        
        if (countAddress != 0) {
            dma->hw_counts = (DMACounts*)countAddress;
        } else {
            printf("failed to acquire counts mapped buffer");
            return EXIT_FAILURE;
        }
    }

    return 0;
}

void litepcie_dma_cleanup(struct litepcie_dma_ctrl *dma)
{
    if (dma->use_reader && dma->reader_enabled)
        litepcie_dma_reader(dma, 0);
    if (dma->use_writer && dma->writer_enabled)
        litepcie_dma_writer(dma, 0);

//    litepcie_release_dma(dma->fds.fd, dma->use_reader, dma->use_writer);

    if (dma->use_reader)
        IOConnectUnmapMemory(dma->fd, LITEPCIE_DMA_READER | 0, mach_task_self(), (mach_vm_address_t)dma->buf_rd);
    if (dma->use_writer)
        IOConnectUnmapMemory(dma->fd, LITEPCIE_DMA_WRITER | 0, mach_task_self(), (mach_vm_address_t)dma->buf_wr);
    if (dma->use_writer || dma->use_reader)
        IOConnectUnmapMemory(dma->fd, LITEPCIE_DMA_COUNTS | 0, mach_task_self(), (mach_vm_address_t)dma->hw_counts);
}

void litepcie_dma_process(struct litepcie_dma_ctrl *dma)
{
    /* set / get dma */
    if (dma->use_reader && !dma->reader_enabled)
        litepcie_dma_reader(dma, 1);
    if (dma->use_writer && !dma->writer_enabled)
        litepcie_dma_writer(dma, 1);
    
    if ((dma->hw_counts->hwWriterCountTotal - dma->writer_sw_count) > 2) {
        /* count available buffers */
        dma->buffers_available_read = MAX(MIN(dma->hw_counts->hwWriterCountTotal - dma->writer_sw_count, DMA_BUFFER_COUNT), 0);
        dma->usr_read_buf_offset = dma->writer_sw_count % DMA_BUFFER_COUNT;
        dma->writer_sw_count = dma->writer_sw_count + dma->buffers_available_read;
    } else {
        dma->buffers_available_read = 0;
    }
    
    if (((int64_t)dma->reader_sw_count - (int64_t)dma->hw_counts->hwReaderCountTotal) < (DMA_BUFFER_COUNT / 2)) {
        /* count available buffers */
        dma->buffers_available_write = MIN(dma->hw_counts->hwReaderCountTotal - dma->reader_sw_count, DMA_BUFFER_COUNT);
        dma->usr_write_buf_offset = dma->reader_sw_count % DMA_BUFFER_COUNT;
        dma->reader_sw_count = dma->reader_sw_count + dma->buffers_available_write;
    } else {
        dma->buffers_available_write = 0;
    }
    
    dma->writer_hw_count = dma->hw_counts->hwWriterCountTotal;
    dma->reader_hw_count = dma->hw_counts->hwReaderCountTotal;
}

char *litepcie_dma_next_read_buffer(struct litepcie_dma_ctrl *dma)
{
//    if (dma->hw_counts->hwWriterCountTotal > dma->writer_sw_count) {
//        /* count available buffers */
//        dma->buffers_available_read = dma->hw_counts->hwWriterCountTotal - dma->writer_sw_count;
//        dma->usr_read_buf_offset = dma->writer_sw_count % DMA_BUFFER_COUNT;
//        dma->writer_sw_count = dma->writer_sw_count + dma->buffers_available_read;
//    } else {
//        dma->buffers_available_read = 0;
//    }
    
    if (!dma->buffers_available_read)
        return NULL;
    
    dma->buffers_available_read--;
    
    uint8_t *ret = dma->buf_rd + dma->usr_read_buf_offset * DMA_BUFFER_SIZE;
    dma->usr_read_buf_offset = (dma->usr_read_buf_offset + 1) % DMA_BUFFER_COUNT;
    return (char*)ret;
}

char *litepcie_dma_next_write_buffer(struct litepcie_dma_ctrl *dma)
{
//    if (dma->hw_counts->hwReaderCountTotal > dma->reader_sw_count) {
//        /* count available buffers */
//        dma->buffers_available_write = DMA_BUFFER_COUNT / 2 + (dma->hw_counts->hwReaderCountTotal - dma->reader_sw_count);
//        dma->usr_write_buf_offset = dma->reader_sw_count % DMA_BUFFER_COUNT;
//        dma->reader_sw_count = dma->reader_sw_count + dma->buffers_available_write;
//    } else {
//        dma->buffers_available_write = 0;
//    }
    
    if (!dma->buffers_available_write)
        return NULL;
    
    dma->buffers_available_write--;
    
    uint8_t *ret = dma->buf_wr + dma->usr_write_buf_offset * DMA_BUFFER_SIZE;
    dma->usr_write_buf_offset = (dma->usr_write_buf_offset + 1) % DMA_BUFFER_COUNT;
    return (char*)ret;
}
