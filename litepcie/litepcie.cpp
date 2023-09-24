//
//  litepcie.cpp
//  litepcie
//
//  Created by skolaut on 9/9/23.
//

#include <time.h>

#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOTimerDispatchSource.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IOUserServer.h>
#include <DriverKit/OSData.h>
#include <DriverKit/DriverKit.h>

#include <PCIDriverKit/PCIDriverKit.h>

#include "litepcie.h"
#include "config.h"
#include "csr.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "litepcie - " fmt "\n", ##__VA_ARGS__)

#define CSR_TO_OFFSET(addr) ((addr) - CSR_BASE)

struct DMADescriptor {
    union {
        struct {
            uint32_t length: 24;     // bits 0..23
            uint8_t disableIRQ: 1;   // bits 24
            uint8_t last: 1;         // bits 25
            uint8_t : 6;             // bits 26..31
        } reg __attribute__((packed));
        uint32_t raw;
    } config;
    uint32_t lsb;            // bits 32..63
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
    
    uint64_t hwReaderCountTotal = 0;
    uint64_t hwReaderCountPrev = 0;
    uint64_t hwWriterCountTotal = 0;
    uint64_t hwWriterCountPrev = 0;
    uint64_t swReaderCount = 0;
    uint64_t swWriterCount = 0;
    
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

struct litepcie_IVars {
    IOPCIDevice* pciDevice;
    IODispatchQueue* defaultDispatchQueue = nullptr;
    IODispatchQueue* interruptDispatchQueue = nullptr;
    IOInterruptDispatchSource* interruptSource;
    DMAChannel* channel[DMA_CHANNEL_COUNT];
    uint64_t interruptCount = 0;
    uint64_t interruptTimePrevBM = 0;
    uint64_t readerPrevBM = 0;
    uint64_t writerPrevBM = 0;
};

kern_return_t litepcie::InitDMAChannel(int chan_idx)
{
    Log("InitDMAChannel() entered");
    
    kern_return_t ret = kIOReturnSuccess;
    uint64_t dmaFlags = kIOMemoryDirectionInOut;
    uint32_t dmaSegmentCount = 1;
    
    IODMACommandSpecification dmaSpecification;

    bzero(&dmaSpecification, sizeof(dmaSpecification));

    dmaSpecification.options = kIODMACommandCreateNoOptions;
    dmaSpecification.maxAddressBits = 64;
    
    
    ivars->channel[chan_idx]->dmaReaderVirtualSegments = IONew(IOAddressSegment*, DMA_BUFFER_COUNT);
    ivars->channel[chan_idx]->dmaReaderPhysicalSegments = IONew(IOAddressSegment*, DMA_BUFFER_COUNT);
    ivars->channel[chan_idx]->dmaReaderCommands = IONew(IODMACommand*, DMA_BUFFER_COUNT);
    ivars->channel[chan_idx]->dmaReaderBuffers = IONew(IOBufferMemoryDescriptor*, DMA_BUFFER_COUNT);
    
    ivars->channel[chan_idx]->dmaWriterVirtualSegments = IONew(IOAddressSegment*, DMA_BUFFER_COUNT);
    ivars->channel[chan_idx]->dmaWriterPhysicalSegments = IONew(IOAddressSegment*, DMA_BUFFER_COUNT);
    ivars->channel[chan_idx]->dmaWriterCommands = IONew(IODMACommand*, DMA_BUFFER_COUNT);
    ivars->channel[chan_idx]->dmaWriterBuffers = IONew(IOBufferMemoryDescriptor*, DMA_BUFFER_COUNT);
    
    
    for (int i = 0; i < DMA_BUFFER_COUNT; i += 1)
    {
        ivars->channel[chan_idx]->dmaReaderVirtualSegments[i] = new IOAddressSegment;
        ivars->channel[chan_idx]->dmaReaderPhysicalSegments[i] = new IOAddressSegment;
        
        ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, DMA_BUFFER_SIZE, 0, &ivars->channel[chan_idx]->dmaReaderBuffers[i]);
        if(ret != kIOReturnSuccess)
        {
            Log("failed to create dma buffer");
            return ret;
        }
        
        ivars->channel[chan_idx]->dmaReaderBuffers[i]->SetLength(DMA_BUFFER_SIZE);
        ivars->channel[chan_idx]->dmaReaderBuffers[i]->GetAddressRange(ivars->channel[chan_idx]->dmaReaderVirtualSegments[i]);
        
        
        IODMACommand::Create(ivars->pciDevice, kIODMACommandCreateNoOptions, &dmaSpecification, &ivars->channel[chan_idx]->dmaReaderCommands[i]);

        dmaSegmentCount = 1;
        dmaFlags = kIOMemoryDirectionInOut;
        
        ret = ivars->channel[chan_idx]->dmaReaderCommands[i]->PrepareForDMA(kIODMACommandPrepareForDMANoOptions,
                                                                      ivars->channel[chan_idx]->dmaReaderBuffers[i],
                                  0,
                                  0,
                                  &dmaFlags,
                                  &dmaSegmentCount,
                                  ivars->channel[chan_idx]->dmaReaderPhysicalSegments[i]);
        
        if(ret != kIOReturnSuccess)
        {
            Log("failed to prepare dma with error: 0x%08x", ret);
            return ret;
        }
    }
    
    for (int i = 0; i < DMA_BUFFER_COUNT; i += 1)
    {
        ivars->channel[chan_idx]->dmaWriterVirtualSegments[i] = new IOAddressSegment;
        ivars->channel[chan_idx]->dmaWriterPhysicalSegments[i] = new IOAddressSegment;
        
        
        ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, DMA_BUFFER_SIZE, 0, &ivars->channel[chan_idx]->dmaWriterBuffers[i]);
        if(ret != kIOReturnSuccess)
        {
            Log("failed to create dma buffer");
            return ret;
        }
        
        ivars->channel[chan_idx]->dmaWriterBuffers[i]->SetLength(DMA_BUFFER_SIZE);
        ivars->channel[chan_idx]->dmaWriterBuffers[i]->GetAddressRange(ivars->channel[chan_idx]->dmaWriterVirtualSegments[i]);
        
        
        IODMACommand::Create(ivars->pciDevice, kIODMACommandCreateNoOptions, &dmaSpecification, &ivars->channel[chan_idx]->dmaWriterCommands[i]);

        dmaSegmentCount = 1;
        dmaFlags = kIOMemoryDirectionInOut;
        
        ret = ivars->channel[chan_idx]->dmaWriterCommands[i]->PrepareForDMA(kIODMACommandPrepareForDMANoOptions,
                                                                      ivars->channel[chan_idx]->dmaWriterBuffers[i],
                                  0,
                                  0,
                                  &dmaFlags,
                                  &dmaSegmentCount,
                                  ivars->channel[chan_idx]->dmaWriterPhysicalSegments[i]);
        
        if(ret != kIOReturnSuccess)
        {
            Log("failed to prepare dma with error: 0x%08x", ret);
            return ret;
        }
    }
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_MSI_ENABLE_ADDR), (1 << ivars->channel[chan_idx]->readerInterrupt) | (1 << ivars->channel[chan_idx]->writerInterrupt));
    
    return ret;
}

kern_return_t litepcie::SetupDMAReaderChannel(int chan_idx)
{
    Log("SetupDMAReaderChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_RESET_ADDR), 1);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LOOP_PROG_N_ADDR), 0);
    
    for (int i = 0; i < DMA_BUFFER_COUNT; i += 1)
    {
        DMADescriptor desc;
        uint64_t readerAddress = ivars->channel[chan_idx]->dmaReaderPhysicalSegments[i]->address;
        uint32_t lsb = (readerAddress >> 0)  & 0xFFFF'FFFF;
        uint32_t msb = (readerAddress >> 32) & 0xFFFF'FFFF;
        desc.lsb = lsb;
        desc.config.reg.last = 1;
        desc.config.reg.length = DMA_BUFFER_SIZE;
        desc.config.reg.disableIRQ = (((i + 1) % DMA_BUFFER_PER_IRQ) == 0) ? 0 : 1; // set bit on when buffer idx of increments of DMA_BUFFER_PER_IRQ
        
        ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_VALUE_ADDR), desc.config.raw);
        ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_VALUE_ADDR) + 4, lsb);
        ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_WE_ADDR), msb);
    }
    
    return ret;
}

kern_return_t litepcie::SetupDMAWriterChannel(int chan_idx)
{
    Log("SetupDMAWriterChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_RESET_ADDR), 1);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LOOP_PROG_N_ADDR), 0);
    
    for (int i = 0; i < DMA_BUFFER_COUNT; i += 1)
    {
        DMADescriptor desc;
        uint64_t readerAddress = ivars->channel[chan_idx]->dmaWriterPhysicalSegments[i]->address;
        uint32_t lsb = (readerAddress >> 0)  & 0xFFFF'FFFF;
        uint32_t msb = (readerAddress >> 32) & 0xFFFF'FFFF;
        desc.lsb = lsb;
        desc.config.reg.last = 1;
        desc.config.reg.length = DMA_BUFFER_SIZE;
        desc.config.reg.disableIRQ = (((i + 1) % DMA_BUFFER_PER_IRQ) == 0) ? 0 : 1; // set bit on when buffer idx of increments of DMA_BUFFER_PER_IRQ
        
        ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_VALUE_ADDR), desc.config.raw);
        ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_VALUE_ADDR) + 4, lsb);
        ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_WE_ADDR), msb);
    }
    
    return ret;
}

kern_return_t litepcie::StartDMAReaderChannel(int chan_idx, bool loop)
{
    Log("StartDMAReaderChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->channel[chan_idx]->hwReaderCountTotal = 0;
    ivars->channel[chan_idx]->hwReaderCountPrev = 0;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LOOP_PROG_N_ADDR), loop ? 1 : 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_ENABLE_ADDR), 1);
    
    return ret;
}

kern_return_t litepcie::StartDMAWriterChannel(int chan_idx, bool loop)
{
    Log("StartDMAWriterChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->channel[chan_idx]->hwWriterCountTotal = 0;
    ivars->channel[chan_idx]->hwWriterCountPrev = 0;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LOOP_PROG_N_ADDR), loop ? 1 : 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_ENABLE_ADDR), 1);
    
    return ret;
}

kern_return_t litepcie::StopDMAReaderChannel(int chan_idx)
{
    Log("StopDMAReaderChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LOOP_PROG_N_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(PCIE_DMA_READER_TABLE_FLUSH_OFFSET), 1);
    
    IOSleep(1);
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(PCIE_DMA_READER_TABLE_FLUSH_OFFSET), 1);
    
    Log("StopDMAReaderChannel() finished");
    return ret;
}

kern_return_t litepcie::StopDMAWriterChannel(int chan_idx)
{
    Log("StopDMAWriterChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LOOP_PROG_N_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(PCIE_DMA_WRITER_TABLE_FLUSH_OFFSET), 1);
    
    IOSleep(1);
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(PCIE_DMA_WRITER_TABLE_FLUSH_OFFSET), 1);
    
    Log("StopDMAWriterChannel() finished");
    return ret;
}

kern_return_t litepcie::StopDMAChannel(int chan_idx)
{
    Log("StopDMAChannel() entered");
    kern_return_t ret = kIOReturnSuccess;
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_ENABLE_ADDR), 0);
    
    Log("StopDMAChannel() finished");
    return ret;
}

void litepcie::CleanupDMAChannel(int chan_idx)
{
    Log("CleanupDMAChannel() entered");
    StopDMAChannel(chan_idx);
//    StopDMAReaderChannel(chan_idx);
//    StopDMAWriterChannel(chan_idx);
    
    
    Log("CleanupDMAChannel() deleting misc descriptor objects");
    for (int i = 0; i < DMA_BUFFER_COUNT; i += 1)
    {
        ivars->channel[chan_idx]->dmaWriterCommands[i]->CompleteDMA(kIODMACommandCompleteDMANoOptions);
        ivars->channel[chan_idx]->dmaReaderCommands[i]->CompleteDMA(kIODMACommandCompleteDMANoOptions);
        
        delete ivars->channel[chan_idx]->dmaWriterVirtualSegments[i];
        delete ivars->channel[chan_idx]->dmaWriterPhysicalSegments[i];
        delete ivars->channel[chan_idx]->dmaReaderVirtualSegments[i];
        delete ivars->channel[chan_idx]->dmaReaderPhysicalSegments[i];
        
        OSSafeReleaseNULL(ivars->channel[chan_idx]->dmaWriterCommands[i]);
        OSSafeReleaseNULL(ivars->channel[chan_idx]->dmaReaderCommands[i]);
        
        OSSafeReleaseNULL(ivars->channel[chan_idx]->dmaWriterBuffers[i]);
        OSSafeReleaseNULL(ivars->channel[chan_idx]->dmaReaderBuffers[i]);
    }
    
    
    Log("CleanupDMAChannel() deleting misc descriptor arrays");
    IODelete(ivars->channel[chan_idx]->dmaWriterCommands, IODMACommand*, DMA_BUFFER_COUNT);
    IODelete(ivars->channel[chan_idx]->dmaWriterBuffers, IOBufferMemoryDescriptor*, DMA_BUFFER_COUNT);
    IODelete(ivars->channel[chan_idx]->dmaWriterVirtualSegments, IOAddressSegment*, DMA_BUFFER_COUNT);
    IODelete(ivars->channel[chan_idx]->dmaWriterPhysicalSegments, IOAddressSegment*, DMA_BUFFER_COUNT);
    
    IODelete(ivars->channel[chan_idx]->dmaReaderCommands, IODMACommand*, DMA_BUFFER_COUNT);
    IODelete(ivars->channel[chan_idx]->dmaReaderBuffers, IOBufferMemoryDescriptor*, DMA_BUFFER_COUNT);
    IODelete(ivars->channel[chan_idx]->dmaReaderVirtualSegments, IOAddressSegment*, DMA_BUFFER_COUNT);
    IODelete(ivars->channel[chan_idx]->dmaReaderPhysicalSegments, IOAddressSegment*, DMA_BUFFER_COUNT);
    
    IOSleep(100);
    
    Log("CleanupDMAChannel() finished");
}

bool litepcie::init(void)
{
    bool result = false;

    Log("init() entered");

    result = super::init();
    if (result != true) {
        Log("super::init failed.");
        goto Exit;
    }

    ivars = IONewZero(litepcie_IVars, 1);
    if (ivars == nullptr) {
        Log("failed to allocate memory for ivars");
        goto Exit;
    }

    Log("init() finished.");
    return true;

Exit:
    return false;
}

kern_return_t
IMPL(litepcie, Start)
{
    kern_return_t ret;
    uint32_t buf[64] = { 0 };
    uint32_t rlevel = 0, wlevel = 0;
    
    uint64_t interruptType = 0;
    uint32_t msiInterruptIndex = 0;
    
    OSAction* interruptOccuredAction;
    
    uint64_t bufferCapacity = DMA_BUFFER_TOTAL_SIZE;
    uint64_t bufferAlignment = 0;
    
    uint64_t dmaFlags = kIOMemoryDirectionInOut;
    uint32_t dmaSegmentCount = 1;
    
    uint64_t readerAddress = 0, writerAddress = 0;
    
    IOAddressSegment physicalAddressSegment = {0};
    
    uint32_t testSize = 8192;

    Log("Start() entered");

    ret = super::Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        Log("super::Start failed with error: 0x%08x", ret);
        goto Exit;
    }

    // try to cast the provider object to a PCI device because thats what it should be
    ivars->pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (ivars->pciDevice == NULL) {
        Log("failed to cast provider PCI device");
        Stop(provider);
        ret = kIOReturnNoDevice;
        goto Exit;
    }

    ivars->pciDevice->retain();

    // open the provider pci device
    ret = ivars->pciDevice->Open(this, 0);
    if (ret != kIOReturnSuccess) {
        Log("provider PCI device could not be opened with error: 0x%08x", ret);
        Stop(provider);
        goto Exit;
    }

    // enable bus master and memory space
    uint16_t command;
    ivars->pciDevice->ConfigurationRead16(kIOPCIConfigurationOffsetCommand, &command);
    ivars->pciDevice->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand,
        command | kIOPCICommandBusMaster | kIOPCICommandMemorySpace);

    // read some misc configuration space values
    ivars->pciDevice->ConfigurationRead32(kIOPCIConfigurationOffsetVendorID, buf);
    ivars->pciDevice->ConfigurationRead32(kIOPCIConfigurationOffsetBaseAddress0, buf + 1);
    ivars->pciDevice->ConfigurationRead32(kIOPCIConfigurationOffsetBaseAddress1, buf + 2);

    Log("Vendor ID: %x", buf[0]);
    Log("BAR0: %x", buf[1]);
    Log("BAR1: %x", buf[2]);

    // test our scratch register
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_CTRL_SCRATCH_ADDR), buf + 3);
    Log("scratch: %x", buf[3]);
    buf[3] = 0xDEADBEEF;
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_CTRL_SCRATCH_ADDR), buf[3]);
    buf[3] = 0xCAFECAFE;
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_CTRL_SCRATCH_ADDR), buf + 3);
    Log("scratch: %x", buf[3]);

    // check led register and set pattern
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_LEDS_BASE), buf + 3);
    Log("led: %x", buf[3]);
    buf[3] = 0b0101;
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_LEDS_BASE), buf[3]);
    buf[3] = 0b0101;
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_LEDS_BASE), buf + 3);
    Log("led: %x", buf[3]);
    
    while((ret = IOInterruptDispatchSource::GetInterruptType(ivars->pciDevice, msiInterruptIndex, &interruptType)) == kIOReturnSuccess)
    {
        Log("checking interrupt: %i type: %llx", msiInterruptIndex, interruptType);
        if ((interruptType & kIOInterruptTypePCIMessaged) != 0) {
            break;
        }
        msiInterruptIndex += 1;
    }
    
    ret = CopyDispatchQueue(kIOServiceDefaultQueueName, &(ivars->defaultDispatchQueue));
    if(ret != kIOReturnSuccess)
    {
        Log("failed to copy queue with error: 0x%08x", ret);
        Stop(provider);
        goto Exit;
    }
    
    ret = IODispatchQueue::Create("interruptDispatchQueue", 0, 0, &ivars->interruptDispatchQueue);
    if(ret != kIOReturnSuccess)
    {
        Log("failed to create queue with error: 0x%08x", ret);
        Stop(provider);
        goto Exit;
    }
    
    ret = IOInterruptDispatchSource::Create(ivars->pciDevice, msiInterruptIndex, ivars->interruptDispatchQueue, &(ivars->interruptSource));
    if(ret != kIOReturnSuccess)
    {
        Log("failed to create interrupt dispatch source");
        Stop(provider);
        goto Exit;
    }
    
    ret = CreateActionInterruptOccurred(sizeof(void*), &interruptOccuredAction);
    if(ret != kIOReturnSuccess)
    {
        Log("failed to create interrupt action");
        Stop(provider);
        goto Exit;
    }
    
    ret = ivars->interruptSource->SetHandler(interruptOccuredAction);
    if(ret != kIOReturnSuccess)
    {
        Log("failed to set interrupt handler");
        Stop(provider);
        return false;
    }

    ret = ivars->interruptSource->SetEnable(true);
    if(ret != kIOReturnSuccess)
    {
        Log("failed to enable interrupt source");
        Stop(provider);
        return false;
    }
    
    IOSleep(10);
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_LOOPBACK_ENABLE_ADDR), 1);
    
    ivars->channel[0] = new DMAChannel;
    ivars->channel[0]->baseAddress = CSR_PCIE_DMA0_BASE;
    ivars->channel[0]->writerInterrupt = PCIE_DMA0_WRITER_INTERRUPT;
    ivars->channel[0]->readerInterrupt = PCIE_DMA0_READER_INTERRUPT;
    InitDMAChannel(0);
    
    SetupDMAWriterChannel(0);
    SetupDMAReaderChannel(0);
    
    StartDMAWriterChannel(0, true);
    StartDMAReaderChannel(0, true);
    
    // register service so we can be access by client app
    ret = RegisterService();
    if (ret != kIOReturnSuccess) {
        Log("failed to register service with error: 0x%08x", ret);
        goto Exit;
    }

Exit:
    Log("Start() finished");
    return ret;
}

void
IMPL(litepcie, InterruptOccurred)
{
    bool printLog = (ivars->interruptCount % 4096) == 0;
    
    
    if (printLog)
        Log("InterruptOccurred() entered");
    uint32_t vector = 0, clear = 0;
    
    DMALoopStatus rstatus, wstatus;
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_MSI_VECTOR_ADDR), &vector);
    if (printLog)
        Log("InterruptOccurred() vector: %x", vector);
    
    for (int i = 0; i < DMA_CHANNEL_COUNT; i += 1)
    {
        uint64_t current_time_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        
        if (vector & (1 << ivars->channel[i]->readerInterrupt))
        {
            clear |= (1 << ivars->channel[i]->readerInterrupt);
            ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(ivars->channel[i]->baseAddress) + PCIE_DMA_READER_TABLE_LOOP_STATUS_OFFSET, &rstatus.raw);
            uint64_t count = rstatus.reg.index * DMA_BUFFER_COUNT + rstatus.reg.index;
            
            
            if(ivars->channel[i]->hwReaderCountPrev > count)
            {
                ivars->channel[i]->hwReaderCountTotal += (DMA_BUFFER_COUNT * (0xFFFF + 1) - ivars->channel[i]->hwReaderCountPrev) + count; // status wraparound
            } else
            {
                ivars->channel[i]->hwReaderCountTotal += (count - ivars->channel[i]->hwReaderCountPrev);
            }
            
            ivars->channel[i]->hwReaderCountPrev = count;
        }
        
        if (vector & (1 << ivars->channel[i]->writerInterrupt))
        {
            clear |= (1 << ivars->channel[i]->writerInterrupt);
            ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(ivars->channel[i]->baseAddress) + PCIE_DMA_WRITER_TABLE_LOOP_STATUS_OFFSET, &wstatus.raw);
            uint64_t count = wstatus.reg.index * DMA_BUFFER_COUNT + wstatus.reg.index;
            
            
            if(ivars->channel[i]->hwWriterCountPrev > count)
            {
                ivars->channel[i]->hwWriterCountTotal += (DMA_BUFFER_COUNT * (0xFFFF + 1) - ivars->channel[i]->hwWriterCountPrev) + count; // status wraparound
            } else
            {
                ivars->channel[i]->hwWriterCountTotal += (count - ivars->channel[i]->hwWriterCountPrev);
            }
            
            ivars->channel[i]->hwWriterCountPrev = count;
        }
        
        if (printLog)
        {
            Log("InterruptOccurred() chan %i hwcounts rd: %lli wr: %lli", i, ivars->channel[i]->hwReaderCountTotal, ivars->channel[i]->hwWriterCountTotal);
            
            uint64_t delta_ns = (current_time_ns - ivars->interruptTimePrevBM);
            double delta_s = delta_ns / 1'000'000'000.0;
            double readerRate = (DMA_BUFFER_SIZE * (ivars->channel[i]->hwReaderCountTotal - ivars->readerPrevBM)) / delta_s;
            double writerRate = (DMA_BUFFER_SIZE * (ivars->channel[i]->hwWriterCountTotal - ivars->writerPrevBM)) / delta_s;
            
            Log("InterruptOccurred() reader MB/s: %0.3f", readerRate / 1'000'000.0);
            Log("InterruptOccurred() writer MB/s: %0.3f", writerRate / 1'000'000.0);
            Log("InterruptOccurred()  total MB/s: %0.3f", (readerRate + writerRate) / 1'000'000.0);
            
            ivars->interruptTimePrevBM = current_time_ns;
            ivars->readerPrevBM = ivars->channel[i]->hwReaderCountTotal;
            ivars->writerPrevBM = ivars->channel[i]->hwWriterCountTotal;
        }
    }
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_MSI_CLEAR_ADDR), clear); // clear interrupts
    
    if (printLog)
        Log("InterruptOccurred() count: %lli", ivars->interruptCount);
    if (printLog)
        Log("InterruptOccurred() finished");
    
    ivars->interruptCount += 1;
}

kern_return_t
IMPL(litepcie, Stop)
{
    kern_return_t ret = kIOReturnSuccess;
    __block _Atomic uint32_t cancelCount = 0;

    Log("Stop() entered");
    
    CleanupDMAChannel(0);
    
    // closes the pci device
    // this also handles clearing bus master enable and
    // memory space enable command bits
    if (ivars->pciDevice != nullptr) {
        ivars->pciDevice->Close(this, 0);
    }

    if (ivars->defaultDispatchQueue != nullptr) {
        ++cancelCount;
    }
    
    if (ivars->interruptDispatchQueue != nullptr) {
        ++cancelCount;
    }

    // If there's somehow nothing to cancel, "Stop" quickly and exit.
    if (cancelCount == 0) {
        ret = Stop(provider, SUPERDISPATCH);
        if (ret != kIOReturnSuccess) {
            Log("Stop() - super::Stop failed with error: 0x%08x.", ret);
        }

        Log("Stop() - Finished.");

        return ret;
    }
    // Otherwise, wait for some Cancels to get completed.

    // Retain the driver instance and the provider so the finalization can properly stop the driver
    this->retain();
    provider->retain();

    // Re-use this block, with each cancel action taking a count off, until the last cancel stops the dext
    void (^finalize)(void) = ^{
        if (__c11_atomic_fetch_sub(&cancelCount, 1U, __ATOMIC_RELAXED) <= 1) {

            kern_return_t status = Stop(provider, SUPERDISPATCH);
            if (status != kIOReturnSuccess) {
                Log("Stop() - super::Stop failed with error: 0x%08x.", status);
            }

            Log("Stop() - Finished.");

            this->release();
            provider->release();
        }
    };

    if (ivars->defaultDispatchQueue != nullptr) {
        ivars->defaultDispatchQueue->Cancel(finalize);
    }
    
    if (ivars->interruptDispatchQueue != nullptr) {
        ivars->interruptDispatchQueue->Cancel(finalize);
    }

    Log("Stop() finished");

    return ret;
}

void litepcie::free(void)
{
    Log("free() entered");

    OSSafeReleaseNULL(ivars->defaultDispatchQueue);
    IOSafeDeleteNULL(ivars, litepcie_IVars, 1);

    super::free();

    Log("free() finished");
}

kern_return_t
IMPL(litepcie, NewUserClient)
{
    kern_return_t ret = kIOReturnSuccess;
    IOService* client = nullptr;

    Log("NewUserClient() entered");

    // create new client object
    ret = Create(this, "UserClientProperties", &client);
    if (ret != kIOReturnSuccess) {
        Log("failed to create UserClientProperties with error: 0x%08x", ret);
        goto Exit;
    }

    // try to cast client object to an IOUserClient
    *userClient = OSDynamicCast(IOUserClient, client);
    if (*userClient == NULL) {
        Log("failed to cast new client");
        client->release();
        ret = kIOReturnError;
        goto Exit;
    }

    Log("NewUserClient() finished");

Exit:
    return ret;
}

kern_return_t litepcie::WriteMemory(uint64_t offset, uint32_t value)
{
    kern_return_t ret = kIOReturnSuccess;
    ivars->pciDevice->MemoryWrite32(0, offset, value);
    return ret;
}

kern_return_t litepcie::ReadMemory(uint64_t offset, uint32_t* dest)
{
    kern_return_t ret = kIOReturnSuccess;
    ivars->pciDevice->MemoryRead32(0, offset, dest);
    return ret;
}
