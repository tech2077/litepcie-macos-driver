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

#include <PCIDriverKit/PCIDriverKit.h>

#include "config.h"
#include "litepcie.h"
#include "litepcie_int.h"
#include "litepcie_ext.h"
#include "litepcie_userclient.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "litepcie_userclient::%s - " fmt "\n", __FUNCTION__, ##__VA_ARGS__)

struct litepcie_userclient_IVars {
    litepcie* litepcie = nullptr;
    IOBufferMemoryDescriptor* rdma;
    IOBufferMemoryDescriptor* wdma;
    IOBufferMemoryDescriptor* cdma;
};

bool litepcie_userclient::init(void)
{
    bool result = false;
    kern_return_t ret = kIOReturnSuccess;
    uint64_t bufsize = 8192 * 2;

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

    ret = ivars->litepcie->CreateReaderBufferDescriptor(0, (IOMemoryDescriptor**)&ivars->rdma);
    if (ret != kIOReturnSuccess) {
        Log("litepcie::CreateReaderBufferDescriptor failed: 0x%x", ret);
        goto Exit;
    }

    ret = ivars->litepcie->CreateWriterBufferDescriptor(0, (IOMemoryDescriptor**)&ivars->wdma);
    if (ret != kIOReturnSuccess) {
        Log("litepcie::CreateWriterBufferDescriptor failed: 0x%x", ret);
        goto Exit;
    }
    
    ret = ivars->litepcie->GetDmaCountDescriptor(0, (IOMemoryDescriptor**)&ivars->cdma);
    if (ret != kIOReturnSuccess) {
        Log("litepcie::GetDmaCountDescriptor failed: 0x%x", ret);
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

    if (ivars->rdma) {
        ivars->rdma->release();
    }

    if (ivars->wdma) {
        ivars->wdma->release();
    }
    
    if (ivars->cdma) {
        ivars->cdma->release();
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
    case LITEPCIE_READ_CSR: {
        ret = HandleReadCSR(arguments);
    } break;
    case LITEPCIE_WRITE_CSR: {
        ret = HandleWriteCSR(arguments);
    } break;

    default:
        break;
    }

Exit:
    Log("ExternalMethod() finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleReadCSR(IOUserClientMethodArguments* arguments)
{
    kern_return_t ret = kIOReturnSuccess;

    ExternalReadWriteCSRStruct* input;
    ExternalReadWriteCSRStruct output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (ExternalReadWriteCSRStruct*)arguments->structureInput->getBytesNoCopy();
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

    // do the thing
    output.addr = input->addr;
    ivars->litepcie->ReadMemory(input->addr, &output.value);

    // send our output out using osdata
    arguments->structureOutput = OSData::withBytes(&output, sizeof(ExternalReadWriteCSRStruct));

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleWriteCSR(IOUserClientMethodArguments* arguments)
{
    kern_return_t ret = kIOReturnSuccess;

    ExternalReadWriteCSRStruct* input;
    ExternalReadWriteCSRStruct output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (ExternalReadWriteCSRStruct*)arguments->structureInput->getBytesNoCopy();
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

    // do the thing
    output.addr = input->addr;
    ivars->litepcie->WriteMemory(input->addr, input->value);
    ivars->litepcie->ReadMemory(output.addr, &output.value);

    // send our output out using osdata
    arguments->structureOutput = OSData::withBytes(&output, sizeof(ExternalReadWriteCSRStruct));

Exit:
    Log("finished");
    return ret;
}

kern_return_t IMPL(litepcie_userclient, CopyClientMemoryForType) //(uint64_t type, uint64_t *options, IOMemoryDescriptor **memory)
{
    Log("entered");

    kern_return_t res = kIOReturnSuccess;

    if (type == LITEPCIE_DMA0_READER) {
        if (ivars->rdma != nullptr) {
            ivars->rdma->retain();
            *memory = (IOMemoryDescriptor*)ivars->rdma;
        }
    } else if (type == LITEPCIE_DMA0_WRITER) {
        if (ivars->wdma != nullptr) {
            ivars->wdma->retain();
            *memory = (IOMemoryDescriptor*)ivars->wdma;
        }
    } else if (type == LITEPCIE_DMA0_COUNTS) {
        if (ivars->cdma != nullptr) {
            ivars->cdma->retain();
            *memory = (IOMemoryDescriptor*)ivars->cdma;
            *options |= kIOUserClientMemoryReadOnly;
        }
    }  else {
        res = this->CopyClientMemoryForType(type, options, memory, SUPERDISPATCH);
    }

    Log("finished");

    return res;
}
