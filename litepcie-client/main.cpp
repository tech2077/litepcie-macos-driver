//
//  main.cpp
//  litepcie-client
//
//  Created by skolaut on 9/10/23.
//
//  Based on example NullDriver client

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/usb/USB.h>
#include <iostream>

enum LitePCIeMessageType {
    LITEPCIE_READ_CSR,
    LITEPCIE_WRITE_CSR,
};

typedef struct {
    uint64_t addr;
    uint32_t value;

} ExternalReadWriteCSRStruct;

inline void PrintErrorDetails(kern_return_t ret)
{
    printf("\tSystem: 0x%02x\n", err_get_system(ret));
    printf("\tSubsystem: 0x%03x\n", err_get_sub(ret));
    printf("\tCode: 0x%04x\n", err_get_code(ret));
}

int main(int argc, const char* argv[])
{
    static const char* dextIdentifier = "litepcie";

    kern_return_t ret = kIOReturnSuccess;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    io_connect_t connection = IO_OBJECT_NULL;

    // Async required variables
    IONotificationPortRef notificationPort = nullptr;
    mach_port_t machNotificationPort = NULL;
    CFRunLoopSourceRef runLoopSource = nullptr;
    io_async_ref64_t asyncRef = {};

    /// - Tag: ClientApp_Connect
    ret = IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceNameMatching(dextIdentifier), &iterator);
    if (ret != kIOReturnSuccess) {
        printf("Unable to find service for identifier with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }

    printf("Searching for dext service...\n");
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        // Open a connection to this user client as a server to that client, and store the instance in "service"
        ret = IOServiceOpen(service, mach_task_self_, kIOHIDServerConnectType, &connection);

        if (ret == kIOReturnSuccess) {
            printf("\tOpened service.\n");
            break;
        } else {
            printf("\tFailed opening service with error: 0x%08x.\n", ret);
        }

        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);

    if (service == IO_OBJECT_NULL) {
        printf("Failed to match to device.\n");
        return EXIT_FAILURE;
    }

    const size_t inputSize = sizeof(ExternalReadWriteCSRStruct);
    ExternalReadWriteCSRStruct input = { .addr = 0x1000 };

    size_t outputSize = sizeof(ExternalReadWriteCSRStruct);
    ExternalReadWriteCSRStruct output = {};

    ret = IOConnectCallStructMethod(connection, LITEPCIE_READ_CSR, &input, inputSize, &output, &outputSize);
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_READ_CSR failed with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }

    printf("result: addr: 0x%llx data: 0x%08x\n", output.addr, output.value);
    
    input.addr = 0x3800;
    input.value = 0x1;
    
    ret = IOConnectCallStructMethod(connection, LITEPCIE_WRITE_CSR, &input, inputSize, &output, &outputSize);
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_WRITE_CSR failed with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }
    
    printf("result: addr: 0x%llx data: 0x%08x\n", output.addr, output.value);
    
    for (int i = 0; i < 0x10; i += 1)
    {
        usleep(1'000'000);
        
        input.addr = 0x3800;
        input.value = i;
        
        ret = IOConnectCallStructMethod(connection, LITEPCIE_WRITE_CSR, &input, inputSize, &output, &outputSize);
        if (ret != kIOReturnSuccess) {
            printf("LITEPCIE_WRITE_CSR failed with error: 0x%08x.\n", ret);
            PrintErrorDetails(ret);
        }
    }

    printf("Exiting");

    return EXIT_SUCCESS;
}
