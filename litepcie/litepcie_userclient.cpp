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
#include "litepcie_userclient.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "litepcie_userclient - " fmt "\n", ##__VA_ARGS__)

struct litepcie_userclient_IVars {
    litepcie* litepcie = nullptr;
};

bool litepcie_userclient::init(void)
{
    bool result = false;

    Log("init() entered");

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

    Log("init() finished.");
    return true;

Exit:
    return false;
}

kern_return_t
IMPL(litepcie_userclient, Start)
{
    kern_return_t ret = kIOReturnSuccess;

    Log("Start() entered");

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
    Log("Start() finished");
    return ret;
}

kern_return_t
IMPL(litepcie_userclient, Stop)
{
    kern_return_t ret = kIOReturnSuccess;

    Log("Stop() entered");

    Log("Stop() finished");

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

Exit:
    Log("ExternalMethod() finished");
    return ret;
}
