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
#include <ctime>
#include <cstdlib>

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
    std::srand((unsigned int)std::time(nullptr));
    
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

    mach_vm_address_t readerAddress = 0;
    mach_vm_address_t writerAddress = 0;
    mach_vm_size_t readerSize = 0;
    mach_vm_size_t writerSize = 0;
    
    ret = IOConnectMapMemory64(connection, 0 /*memoryType*/, mach_task_self(), &readerAddress, &readerSize, kIOMapAnywhere);
    printf("reader ret: 0x%x\n", ret);
    ret = IOConnectMapMemory64(connection, 1 /*memoryType*/, mach_task_self(), &writerAddress, &writerSize, kIOMapAnywhere);
    printf("writer ret: 0x%x\n", ret);
    
    printf("reader addr: 0x%llx writer addr: 0x%llx\n", readerAddress, writerAddress);
    
    if (readerAddress != 0 && writerAddress != 0) {
        uint8_t* readerBuffer = reinterpret_cast<uint8_t*>(readerAddress);
        uint8_t* writerBuffer = reinterpret_cast<uint8_t*>(writerAddress);
        
        auto testIdx = 0x2;
        
        printf("reader buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", readerSize, readerBuffer[0], readerBuffer[testIdx]);
        printf("writer buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", writerSize, writerBuffer[0], writerBuffer[testIdx]);
        printf("write test value\n");
        readerBuffer[0] = std::rand();
        readerBuffer[testIdx] = std::rand();
        printf("reader buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", readerSize, readerBuffer[0], readerBuffer[testIdx]);
        printf("writer buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", writerSize, writerBuffer[0], writerBuffer[testIdx]);
        printf("short sleep\n");
        usleep(300'000);
        printf("reader buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", readerSize, readerBuffer[0], readerBuffer[testIdx]);
        printf("writer buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", writerSize, writerBuffer[0], writerBuffer[testIdx]);
        
        std::srand(1);
        
//        for (int i = 0; i < readerSize; i += 1) {
//            readerBuffer[i] = std::rand();
//            writerBuffer[i] = readerBuffer[i];
//        }
        
        usleep(500'000);
        
        std::srand(1);
        
        for (int i = 0; i < writerSize; i += 0x1) {
            if (writerBuffer[i] != readerBuffer[i]) {
                printf("buffer check failed at idx: 0x%x with vals 0x%x 0x%x\n", i, writerBuffer[i], readerBuffer[i]);
            } else {
                printf("buffer check passed at idx: 0x%x with vals 0x%x 0x%x\n", i, writerBuffer[i], readerBuffer[i]);
            }
        }
    //
    //
    //    printf("result: addr: 0x%llx data: 0x%08x\n", output.addr, output.value);
    //
    //    for (int i = 0; i < 0x10; i += 1) {
    //        usleep(100'000);
    //
    //        input.addr = 0x3800;
    //        input.value = i;
    //
    //        ret = IOConnectCallStructMethod(connection, LITEPCIE_WRITE_CSR, &input, inputSize, &output, &outputSize);
    //        if (ret != kIOReturnSuccess) {
    //            printf("LITEPCIE_WRITE_CSR failed with error: 0x%08x.\n", ret);
    //            PrintErrorDetails(ret);
    //        }
    //    }
    }

    IOConnectUnmapMemory(connection, 0, mach_task_self(), readerAddress);
    IOConnectUnmapMemory(connection, 1, mach_task_self(), writerAddress);

    printf("Exiting");

    return EXIT_SUCCESS;
}
