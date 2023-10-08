//
//  litepcie_ext.h
//  litepcie
//
//  Created by skolaut on 10/7/23.
//

#ifndef litepcie_ext_h
#define litepcie_ext_h

#include "csr.h"

struct DMACounts {
    uint64_t hwReaderCountTotal = 0;
    uint64_t hwReaderCountPrev = 0;
    uint64_t hwWriterCountTotal = 0;
    uint64_t hwWriterCountPrev = 0;
} __attribute__((packed));

enum LitePCIeMessageType {
    LITEPCIE_READ_CSR,
    LITEPCIE_WRITE_CSR,
};

enum LitePCIeMemoryType {
    LITEPCIE_DMA0_READER,
    LITEPCIE_DMA0_WRITER,
    LITEPCIE_DMA0_COUNTS,
};

typedef struct {
    uint64_t addr;
    uint32_t value;
} ExternalReadWriteCSRStruct;

#endif /* litepcie_ext_h */
