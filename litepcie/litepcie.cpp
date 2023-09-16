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

#include "litepcie.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "litepcie - " fmt "\n", ##__VA_ARGS__)

struct litepcie_IVars {
    IOPCIDevice* pciDevice;
    IODispatchQueue* dispatchQueue = nullptr;
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

    Log("Start() entered");

    ret = super::Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        Log("super::Start failed with error: 0x%08x", ret);
        goto Exit;
    }

    //    ivars->dispatchQueue = nullptr;
    //    ret = this->CopyDispatchQueue(kIOServiceDefaultQueueName, &ivars->dispatchQueue);
    //    if (ret == KERN_SUCCESS && ivars->dispatchQueue != nullptr) {
    //        ret = this->SetDispatchQueue(kIOUserClientQueueNameExternalMethod, ivars->dispatchQueue);
    //        if (ret != kIOReturnSuccess) {
    //            Log("could not set dispatch queue with error: 0x%08x", ret);
    //            goto Exit;
    //        }
    //    }
    //    OSSafeReleaseNULL(ivars->dispatchQueue);

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
    ivars->pciDevice->MemoryRead32(0, 0x4, buf + 3);
    Log("scratch: %x", buf[3]);
    buf[3] = 0xDEADBEEF;
    ivars->pciDevice->MemoryWrite32(0, 0x4, buf[3]);
    buf[3] = 0xCAFECAFE;
    ivars->pciDevice->MemoryRead32(0, 0x4, buf + 3);
    Log("scratch: %x", buf[3]);

    // check led register and set pattern
    ivars->pciDevice->MemoryRead32(0, 0x3800, buf + 3);
    Log("led: %x", buf[3]);
    buf[3] = 0b0101;
    ivars->pciDevice->MemoryWrite32(0, 0x3800, buf[3]);
    buf[3] = 0b0101;
    ivars->pciDevice->MemoryRead32(0, 0x3800, buf + 3);
    Log("led: %x", buf[3]);

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
