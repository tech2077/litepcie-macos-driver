//
//  litepcie_ext.h
//  litepcie
//
//  Created by skolaut on 10/7/23.
//

#ifndef litepcie_ext_h
#define litepcie_ext_h

#include <stdbool.h>

#include "csr.h"

#define CSR_TO_OFFSET(addr) ((addr)-CSR_BASE)

enum LitePCIeMessageType {
    LITEPCIE_CONFIG_DMA_READER_CHANNEL,
    LITEPCIE_CONFIG_DMA_WRITER_CHANNEL,
    LITEPCIE_READ_CSR,
    LITEPCIE_WRITE_CSR,
    LITEPCIE_ICAP,
    LITEPCIE_FLASH,
};

enum LitePCIeMemoryType {
    LITEPCIE_DMA_READER = 0x00010000,
    LITEPCIE_DMA_WRITER = 0x00020000,
    LITEPCIE_DMA_COUNTS = 0x00040000,
};

#define LITEPCIE_DMA_MEMORY(type, dma_channel) ((uint64_t)(type & dma_channel))

typedef struct DMACounts {
    uint64_t hwReaderCountTotal;
    uint64_t hwReaderCountPrev;
    uint64_t hwWriterCountTotal;
    uint64_t hwWriterCountPrev;
} __attribute__((packed)) DMACounts;

typedef struct LitePCIeConfigDmaChannelData {
    uint32_t channel;
    bool enable;
} __attribute__((packed)) LitePCIeConfigDmaChannelData;

typedef struct LitePCIeFlashCallData {
    uint32_t tx_len; /* 8 to 40 */
    uint64_t tx_data; /* 8 to 40 bits */
    uint64_t rx_data; /* 40 bits */
} __attribute__((packed)) LitePCIeFlashCallData;

typedef struct LitePCIeICAPCallData {
    uint8_t addr;
    uint32_t data;
} __attribute__((packed)) LitePCIeICAPCallData;

#endif /* litepcie_ext_h */
