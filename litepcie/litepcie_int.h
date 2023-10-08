//
//  structs.h
//  litepcie
//
//  Created by skolaut on 10/6/23.
//

#ifndef structs_h
#define structs_h

#include "litepcie_ext.h"

#define CSR_TO_OFFSET(addr) ((addr)-CSR_BASE)

struct DMADescriptor {
    union {
        struct {
            uint32_t length : 24; // bits 0..23
            uint8_t disableIRQ : 1; // bits 24
            uint8_t last : 1; // bits 25
            uint8_t : 6; // bits 26..31
        } reg __attribute__((packed));
        uint32_t raw;
    } config;
    uint32_t lsb; // bits 32..63
};

union DMALoopStatus {
    struct {
        uint16_t count;
        uint16_t index;
    } reg __attribute__((packed));
    uint32_t raw;
};

struct DMAChannel {
    uint64_t baseAddress;
    
    IOBufferMemoryDescriptor* dmaCountsBuffer;
    DMACounts* dmaCounts;

    uint32_t readerInterrupt;
    uint32_t writerInterrupt;

    IODMACommand** dmaReaderCommands;
    IOBufferMemoryDescriptor** dmaReaderBuffers;
    IOAddressSegment** dmaReaderVirtualSegments;
    IOAddressSegment** dmaReaderPhysicalSegments;

    IODMACommand** dmaWriterCommands;
    IOBufferMemoryDescriptor** dmaWriterBuffers;
    IOAddressSegment** dmaWriterVirtualSegments;
    IOAddressSegment** dmaWriterPhysicalSegments;
};

#endif /* structs_h */
