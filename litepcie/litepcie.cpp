//
//  litepcie.cpp
//  litepcie
//
//  Created by skolaut on 9/9/23.
//

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

struct litepcie_IVars {
    IOPCIDevice* pciDevice;
    IODispatchQueue* dispatchQueue = nullptr;
    IOInterruptDispatchSource* interruptSource;
    uint64_t hwReaderCount = 0;
    uint64_t hwWriterCount = 0;
    uint64_t swReaderCount = 0;
    uint64_t swWriterCount = 0;
};

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
    
    ret = CopyDispatchQueue(kIOServiceDefaultQueueName, &(ivars->dispatchQueue));
    if(ret != kIOReturnSuccess)
    {
        Log("failed to copy queue with error: 0x%08x", ret);
        Stop(provider);
        goto Exit;
    }
    
    ret = IOInterruptDispatchSource::Create(ivars->pciDevice, msiInterruptIndex, ivars->dispatchQueue, &(ivars->interruptSource));
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
    
    IOAddressSegment virtualAddressSegment;
    IOBufferMemoryDescriptor* dmaBuffer;
    ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, bufferCapacity, bufferAlignment, &dmaBuffer);
    if(ret != kIOReturnSuccess)
    {
        Log("failed to create dma buffer");
        Stop(provider);
        return false;
    }
    
    dmaBuffer->SetLength(bufferCapacity);
    dmaBuffer->GetAddressRange(&virtualAddressSegment);
    
    for(uint64_t i = 0; i < 8192; i += 1)
    {
        reinterpret_cast<uint8_t*>(virtualAddressSegment.address)[i] = i % 0xFF;
    }
    
    for(uint64_t i = 8192; i < 8192 * 2; i += 1)
    {
        reinterpret_cast<uint8_t*>(virtualAddressSegment.address)[i] = 0xAF;
    }
    
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_MSI_ENABLE_ADDR), (1 << 1) | (1 << 0));
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_LOOPBACK_ENABLE_ADDR), 1);
    
    
    
    
    
    IODMACommand* dmaCommand;
    IODMACommandSpecification dmaSpecification;

    bzero(&dmaSpecification, sizeof(dmaSpecification));

    dmaSpecification.options = kIODMACommandCreateNoOptions;
    dmaSpecification.maxAddressBits = 32;
    
    IODMACommand::Create(ivars->pciDevice, kIODMACommandCreateNoOptions, &dmaSpecification, &dmaCommand);

    
    
    dmaSegmentCount = 1;
    ret = dmaCommand->PrepareForDMA(kIODMACommandPrepareForDMANoOptions,
                              dmaBuffer,
                              0,
                              0,
                              &dmaFlags,
                              &dmaSegmentCount,
                              &physicalAddressSegment);
    
    if(ret != kIOReturnSuccess)
    {
        Log("failed to prepare dma with error: 0x%08x", ret);
        Stop(provider);
        return false;
    }
    
    readerAddress = physicalAddressSegment.address;
    writerAddress = physicalAddressSegment.address + 8192;
    

    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_RESET_ADDR), 1);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LOOP_PROG_N_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_RESET_ADDR), 1);


    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_VALUE_ADDR),
                                    DMA_LAST_DISABLE |
//                                    DMA_IRQ_DISABLE |
                                    4096);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_VALUE_ADDR) + 4,
                                    (readerAddress >>   0) & 0xffffffff);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_WE_ADDR),
                                    (readerAddress >>  32) & 0xffffffff);


    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_ENABLE_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_RESET_ADDR), 1);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LOOP_PROG_N_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_RESET_ADDR), 1);

    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_VALUE_ADDR),
                                    DMA_LAST_DISABLE |
//                                    DMA_IRQ_DISABLE |
                                    4096);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_VALUE_ADDR) + 4,
                                    (writerAddress >>   0) & 0xffffffff);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_WE_ADDR),
                                    (writerAddress >>  32) & 0xffffffff);

    
    Log("reader address: %llx", readerAddress);
    Log("writer address: %llx", writerAddress);

    
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LEVEL_ADDR), &rlevel);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LEVEL_ADDR), &wlevel);
    Log("descriptor levels: %x %x", rlevel, wlevel);
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_READER_FIFO_CONTROL_ADDR), &rlevel);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_WRITER_FIFO_CONTROL_ADDR), &wlevel);
    Log("fifo control: %x %x", rlevel, wlevel);
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_READER_FIFO_STATUS_ADDR), &rlevel);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_WRITER_FIFO_STATUS_ADDR), &wlevel);
    Log("fifo status: %x %x", rlevel, wlevel);
    
    
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LOOP_PROG_N_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_ENABLE_ADDR), 1);
    IOSleep(100);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LOOP_PROG_N_ADDR), 0);
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_ENABLE_ADDR), 1);
    IOSleep(100);
    
    for(uint64_t i = 0; i < 5; i += 1)
    {
        Log("Read buffer    idx: %lli val: 0x%x", i, reinterpret_cast<uint8_t*>(virtualAddressSegment.address)[i]);
        Log("Written buffer idx: %lli val: 0x%x", i, reinterpret_cast<uint8_t*>(virtualAddressSegment.address)[8192 + i]);
    }
    
    Log("loopback success: %i",
        memcmp(reinterpret_cast<void*>(virtualAddressSegment.address), reinterpret_cast<void*>(virtualAddressSegment.address + 8192), 4096));
    
    ret = dmaCommand->CompleteDMA(kIODMACommandCompleteDMANoOptions);
    if(ret != kIOReturnSuccess)
    {
        Log("failed to complete dma");
        Stop(provider);
        return false;
    }
    
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
    Log("InterruptOccurred() entered");
    uint32_t rval = 0, wval = 0;
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_MSI_VECTOR_ADDR), &rval);
    Log("InterruptOccurred() vector: %x", rval);
    
    ivars->hwReaderCount +=  (rval & 0x1) ? 1 : 0;
    ivars->hwWriterCount +=  (rval & 0x2) ? 1 : 0;
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LOOP_STATUS_ADDR), &rval);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LOOP_STATUS_ADDR), &wval);
    Log("InterruptOccurred() status: %x %x", rval, wval);
    
    uint32_t rlevel = 0, wlevel = 0;
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_READER_TABLE_LEVEL_ADDR), &rlevel);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_WRITER_TABLE_LEVEL_ADDR), &wlevel);
    Log("InterruptOccurred() level: %x %x", rlevel, wlevel);
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_READER_FIFO_CONTROL_ADDR), &rlevel);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_WRITER_FIFO_CONTROL_ADDR), &wlevel);
    Log("InterruptOccurred() fifo control: %x %x", rlevel, wlevel);
    
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_READER_FIFO_STATUS_ADDR), &rlevel);
    ivars->pciDevice->MemoryRead32(0, CSR_TO_OFFSET(CSR_PCIE_DMA0_BUFFERING_WRITER_FIFO_STATUS_ADDR), &wlevel);
    Log("InterruptOccurred() fifo status: %x %x", rlevel, wlevel);
    
    ivars->pciDevice->MemoryWrite32(0, CSR_TO_OFFSET(CSR_PCIE_MSI_CLEAR_ADDR), 3); // clear interrupts 1 and 0
    
    Log("InterruptOccurred() finished");
}

kern_return_t
IMPL(litepcie, Stop)
{
    kern_return_t ret = kIOReturnSuccess;
    __block _Atomic uint32_t cancelCount = 0;

    Log("Stop() entered");

    // closes the pci device
    // this also handles clearing bus master enable and
    // memory space enable command bits
    if (ivars->pciDevice != nullptr) {
        ivars->pciDevice->Close(this, 0);
    }

    if (ivars->dispatchQueue != nullptr) {
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

    if (ivars->dispatchQueue != nullptr) {
        ivars->dispatchQueue->Cancel(finalize);
    }

    Log("Stop() finished");

    return ret;
}

void litepcie::free(void)
{
    Log("free() entered");

    OSSafeReleaseNULL(ivars->dispatchQueue);
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
