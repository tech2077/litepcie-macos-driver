#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOTimerDispatchSource.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IOUserServer.h>
#include <DriverKit/OSData.h>

#include <PCIDriverKit/PCIDriverKit.h>

#include "config.h"
#include "litepcie.h"
#include "litepcie_int.h"
#include "litepcie_ext.h"
#include "litepcie_userclient.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "litepcie_userclient::%s - " fmt "\n", __FUNCTION__, ##__VA_ARGS__)

struct litepcie_userclient_IVars {
    litepcie* litepcie = nullptr;
    IOBufferMemoryDescriptor* rdma[16] = {nullptr};
    IOBufferMemoryDescriptor* wdma[16] = {nullptr};
    IOBufferMemoryDescriptor* cdma[16] = {nullptr};
};

bool litepcie_userclient::init(void)
{
    bool result = false;

    Log("entered");

    result = super::init();
    if (result != true) {
        Log("super::init failed.");
        goto Exit;
    }

    ivars = IONewZero(litepcie_userclient_IVars, 1);
    if (ivars == nullptr) {
        Log("failed to allocate memory for ivars");
        goto Exit;
    }

    Log("finished.");
    return true;

Exit:
    return false;
}

kern_return_t
IMPL(litepcie_userclient, Start)
{
    kern_return_t ret = kIOReturnSuccess;

    Log("entered");

    ret = super::Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        Log("super::Start failed with error: 0x%08x", ret);
        goto Exit;
    }

    // try to cast the provider object to a PCI device because thats what it should be
    ivars->litepcie = OSDynamicCast(litepcie, provider);
    if (ivars->litepcie == NULL) {
        Log("failed to cast provider litepcie driver");
        ret = kIOReturnNoDevice;
        goto Exit;
    }

Exit:
    Log("finished");
    return ret;
}

kern_return_t
IMPL(litepcie_userclient, Stop)
{
    kern_return_t ret = kIOReturnSuccess;

    Log("entered");
    
    for (int i = 0; i < 16; i += 1) {
        if (ivars->rdma[i] != nullptr) {
            ivars->rdma[i]->release();
        }

        if (ivars->wdma[i] != nullptr) {
            ivars->wdma[i]->release();
        }
        
        if (ivars->cdma[i] != nullptr) {
            ivars->cdma[i]->release();
        }
    }

    Log("finished");

    return ret;
}

void litepcie_userclient::free(void)
{
    Log("free() entered");

    IOSafeDeleteNULL(ivars, litepcie_userclient_IVars, 1);

    super::free();

    Log("free() finished");
}

kern_return_t litepcie_userclient::ExternalMethod(uint64_t selector, IOUserClientMethodArguments* arguments, const IOUserClientMethodDispatch* dispatch, OSObject* target, void* reference)
{
    kern_return_t ret = kIOReturnSuccess;
    Log("ExternalMethod() entered");
    Log("ExternalMethod() selector: %lli", selector);

    switch (selector) {
    case LITEPCIE_CONFIG_DMA_READER_CHANNEL: {
        ret = HandleConfigDmaChannel(arguments, true);
    } break;
    case LITEPCIE_CONFIG_DMA_WRITER_CHANNEL: {
        ret = HandleConfigDmaChannel(arguments, false);
    } break;
    case LITEPCIE_READ_CSR: {
        ret = HandleReadCSR(arguments);
    } break;
    case LITEPCIE_WRITE_CSR: {
        ret = HandleWriteCSR(arguments);
    } break;
    case LITEPCIE_ICAP: {
        ret = HandleICAP(arguments);
    } break;
    case LITEPCIE_FLASH: {
        ret = HandleFlash(arguments);
    } break;

    default:
        break;
    }

Exit:
    Log("ExternalMethod() finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleConfigDmaChannel(IOUserClientMethodArguments* arguments, bool is_reader)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;

    LitePCIeConfigDmaChannelData* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeConfigDmaChannelData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (input == nullptr) {
        Log("input struct was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }
    
    if (is_reader) {
        if (ivars->litepcie->IsDMAReaderChannelEnabled(input->channel) != input->enable) {
            if (input->enable){
                ivars->litepcie->SetupDMAReaderChannel(input->channel);
                ivars->litepcie->StartDMAReaderChannel(input->channel, true);
            } else {
                ivars->litepcie->StopDMAReaderChannel(input->channel);
            }
        }
    } else {
        if (ivars->litepcie->IsDMAWriterChannelEnabled(input->channel) != input->enable) {
            if (input->enable){
                ivars->litepcie->SetupDMAWriterChannel(input->channel);
                ivars->litepcie->StartDMAWriterChannel(input->channel, true);
            } else {
                ivars->litepcie->StopDMAWriterChannel(input->channel);
            }
        }
    }
    
Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleFlash(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;

    LitePCIeFlashCallData* input;
    LitePCIeFlashCallData output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeFlashCallData*)arguments->structureInput->getBytesNoCopy();
        output.tx_len = input->tx_len;
        output.tx_data = input->tx_data;
        output.rx_data = input->rx_data;
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (input == nullptr) {
        Log("input struct was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }
    
    if (input->tx_len < 8 || input->tx_len > 40) {
        Log("tx_len not >= 8 or <= 40");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

#ifdef CSR_FLASH_SPI_MOSI_ADDR
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MOSI_ADDR), input->tx_data >> 32);
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MOSI_ADDR) + 4, (uint32_t)(input->tx_data & 0xFF'FF'FF'FF));
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_CONTROL_ADDR), SPI_CTRL_START | (input->tx_len * SPI_CTRL_LENGTH));
    IODelay(16);
    for (int i = 0; i < SPI_TIMEOUT; i += 1) {
        uint32_t val;
        ivars->litepcie->ReadMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MOSI_ADDR), &val);
        if(val & SPI_STATUS_DONE) {
            break;
        }
        IODelay(1);
    }
    uint32_t lsb, msb;
    ivars->litepcie->ReadMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MISO_ADDR), &msb);
    ivars->litepcie->ReadMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MISO_ADDR) + 4, &lsb);
    output.rx_data = ((uint64_t)msb << 32) | lsb;
#endif
    
    // send our output out using osdata
    arguments->structureOutput = OSData::withBytes(&output, sizeof(LitePCIeFlashCallData));

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleICAP(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;

    LitePCIeICAPCallData* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeICAPCallData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (input == nullptr) {
        Log("input struct was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_ICAP_ADDR_ADDR), input->addr);
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_ICAP_DATA_ADDR), input->data);
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_ICAP_WRITE_ADDR), 1);

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleReadCSR(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    
    const uint64_t* input;
    uint64_t output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->scalarInput != nullptr && arguments->scalarInputCount == 1) {
        input = arguments->scalarInput;
    } else {
        Log("scalarInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    ivars->litepcie->ReadMemory(input[0], (uint32_t*)&output);

    arguments->scalarOutput[0] = output;

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleWriteCSR(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    
    const uint64_t* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->scalarInput != nullptr && arguments->scalarInputCount == 2) {
        input = arguments->scalarInput;
    } else {
        Log("scalarInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }
    
    ivars->litepcie->WriteMemory(input[0], (uint32_t)input[1]);

Exit:
    Log("finished");
    return ret;
}

kern_return_t IMPL(litepcie_userclient, CopyClientMemoryForType) //(uint64_t type, uint64_t *options, IOMemoryDescriptor **memory)
{
    Log("entered");

    kern_return_t ret = kIOReturnSuccess;
    
    uint8_t dma_channel = type & 0xF;
    
    if (type & LITEPCIE_DMA_READER) {
        if (ivars->rdma[dma_channel] != nullptr) {
            ivars->rdma[dma_channel]->retain();
            *memory = (IOMemoryDescriptor*)(ivars->rdma[dma_channel]);
        } else {
            ret = ivars->litepcie->CreateReaderBufferDescriptor(dma_channel, (IOMemoryDescriptor**)&(ivars->rdma[dma_channel]));
            if (ret != kIOReturnSuccess) {
                Log("litepcie::CreateReaderBufferDescriptor failed: 0x%x", ret);
            } else {
                ivars->rdma[dma_channel]->retain();
                *memory = (IOMemoryDescriptor*)(ivars->rdma[dma_channel]);
            }
        }
    } else if (type & LITEPCIE_DMA_WRITER) {
        if (ivars->wdma[dma_channel] != nullptr) {
            ivars->wdma[dma_channel]->retain();
            *memory = (IOMemoryDescriptor*)(ivars->wdma[dma_channel]);
        } else {
            ret = ivars->litepcie->CreateWriterBufferDescriptor(dma_channel, (IOMemoryDescriptor**)&(ivars->wdma[dma_channel]));
            if (ret != kIOReturnSuccess) {
                Log("litepcie::CreateWriterBufferDescriptor failed: 0x%x", ret);
            } else {
                ivars->wdma[dma_channel]->retain();
                *memory = (IOMemoryDescriptor*)(ivars->wdma[dma_channel]);
            }
        }
    } else if (type & LITEPCIE_DMA_COUNTS) {
        if (ivars->cdma[dma_channel] != nullptr) {
            ivars->cdma[dma_channel]->retain();
            *memory = (IOMemoryDescriptor*)(ivars->cdma[dma_channel]);
            *options |= kIOUserClientMemoryReadOnly;
        } else {
            ret = ivars->litepcie->GetDmaCountDescriptor(dma_channel, (IOMemoryDescriptor**)&(ivars->cdma[dma_channel]));
            if (ret != kIOReturnSuccess) {
                Log("litepcie::GetDmaCountDescriptor failed: 0x%x", ret);
            } else {
                ivars->cdma[dma_channel]->retain();
                *memory = (IOMemoryDescriptor*)(ivars->cdma[dma_channel]);
                *options |= kIOUserClientMemoryReadOnly;
            }
        }
    }  else {
        ret = this->CopyClientMemoryForType(type, options, memory, SUPERDISPATCH);
    }

    Log("finished");

    return ret;
}
