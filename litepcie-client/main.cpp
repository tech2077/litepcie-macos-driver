#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/usb/USB.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <cstring>

#include "csr.h"
#include "config.h"
#include "litepcie_ext.h"


inline void PrintErrorDetails(kern_return_t ret)
{
    printf("\tSystem: 0x%02x\n", err_get_system(ret));
    printf("\tSubsystem: 0x%03x\n", err_get_sub(ret));
    printf("\tCode: 0x%04x\n", err_get_code(ret));
}

void writel(io_connect_t connection, uint32_t addr, uint32_t val)
{
    kern_return_t ret = kIOReturnSuccess;

    uint32_t olen = 0;
    uint64_t input[2] = { addr, val };
    ret = IOConnectCallScalarMethod(connection, LITEPCIE_WRITE_CSR, input, 2, nullptr, &olen);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_WRITE_CSR failed with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }
}

uint32_t readl(io_connect_t connection, uint32_t addr)
{
    kern_return_t ret = kIOReturnSuccess;
    
    uint32_t olen = 1;
    uint64_t output = 0;
    uint64_t input = addr;
    
    ret = IOConnectCallScalarMethod(connection, LITEPCIE_READ_CSR, &input, 1, &output, &olen);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_READ_CSR failed with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }
    
    return (uint32_t)output;
}

void config_reader_dma(io_connect_t connection, uint32_t chan_idx, bool enable)
{
    kern_return_t ret = kIOReturnSuccess;
    
    LitePCIeConfigDmaChannelData data {
        .channel = chan_idx,
        .enable = enable,
    };
    
    ret = IOConnectCallStructMethod(connection, LITEPCIE_CONFIG_DMA_READER_CHANNEL, &data, sizeof(LitePCIeConfigDmaChannelData), nullptr, 0);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_CONFIG_DMA_READER_CHANNEL failed with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }
}

void config_writer_dma(io_connect_t connection, uint32_t chan_idx, bool enable)
{
    kern_return_t ret = kIOReturnSuccess;
    
    LitePCIeConfigDmaChannelData data {
        .channel = chan_idx,
        .enable = enable,
    };
    
    ret = IOConnectCallStructMethod(connection, LITEPCIE_CONFIG_DMA_WRITER_CHANNEL, &data, sizeof(LitePCIeConfigDmaChannelData), nullptr, 0);
    
    if (ret != kIOReturnSuccess) {
        printf("LITEPCIE_CONFIG_DMA_WRITER_CHANNEL failed with error: 0x%08x.\n", ret);
        PrintErrorDetails(ret);
    }
}

int main(int argc, const char* argv[])
{
    std::srand((unsigned int)std::time(nullptr));

    static const char* dextIdentifier = "litepcie";

    kern_return_t ret = kIOReturnSuccess;
    io_iterator_t iterator = IO_OBJECT_NULL;
    io_service_t service = IO_OBJECT_NULL;
    io_connect_t connection = IO_OBJECT_NULL;

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
    
    config_reader_dma(connection, 0, true);
    config_writer_dma(connection, 0, true);

    printf("result: addr: 0x%lx data: 0x%08x\n", CSR_TO_OFFSET(CSR_DNA_BASE), readl(connection, CSR_TO_OFFSET(CSR_DNA_BASE)));

    writel(connection, CSR_TO_OFFSET(CSR_LEDS_BASE), 0x1);

    mach_vm_address_t readerAddress = 0;
    mach_vm_address_t writerAddress = 0;
    mach_vm_address_t countAddress = 0;
    mach_vm_size_t readerSize = 0;
    mach_vm_size_t writerSize = 0;
    mach_vm_size_t countsize = 0;

    ret = IOConnectMapMemory64(connection, LITEPCIE_DMA_READER | 0, mach_task_self(), &readerAddress, &readerSize, kIOMapAnywhere);
    printf("reader ret: 0x%x\n", ret);
    ret = IOConnectMapMemory64(connection, LITEPCIE_DMA_WRITER | 0, mach_task_self(), &writerAddress, &writerSize, kIOMapAnywhere);
    printf("writer ret: 0x%x\n", ret);
    ret = IOConnectMapMemory64(connection, LITEPCIE_DMA_COUNTS | 0, mach_task_self(), &countAddress, &countsize, kIOMapAnywhere);
    printf("counts ret: 0x%x\n", ret);

    printf("reader addr: 0x%llx writer addr: 0x%llx\n", readerAddress, writerAddress);

    if (readerAddress != 0 && writerAddress != 0 && countsize != 0) {
        uint8_t* readerBuffer = reinterpret_cast<uint8_t*>(readerAddress);
        uint8_t* writerBuffer = reinterpret_cast<uint8_t*>(writerAddress);
        DMACounts* dmaCounts = reinterpret_cast<DMACounts*>(countAddress);

        printf("counts rd: %llu wr: %llu\n", dmaCounts->hwReaderCountTotal, dmaCounts->hwWriterCountTotal);

        uint8_t* tmpBuffer = new uint8_t[readerSize];

        auto testIdx = 0x2;

        printf("reader buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", readerSize, readerBuffer[0], readerBuffer[testIdx]);
        printf("writer buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", writerSize, writerBuffer[0], writerBuffer[testIdx]);
        printf("write test value\n");
        readerBuffer[0] = std::rand();
        readerBuffer[testIdx] = std::rand();
        printf("reader buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", readerSize, readerBuffer[0], readerBuffer[testIdx]);
        printf("writer buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", writerSize, writerBuffer[0], writerBuffer[testIdx]);
        printf("short sleep\n");
        usleep(200'000);
        printf("reader buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", readerSize, readerBuffer[0], readerBuffer[testIdx]);
        printf("writer buffer size: 0x%llx [0]: 0x%x [0x1FFFFF]: 0x%x\n", writerSize, writerBuffer[0], writerBuffer[testIdx]);

        std::srand(1);

        for (int i = 0; i < readerSize; i += 1) {
            tmpBuffer[i] = std::rand();
        }

        auto trials = 1000;
        double avgTime = 0;

        avgTime = 0;
        uint64_t swReaderCount = dmaCounts->hwReaderCountTotal;
        for (int i = 0; i < trials; i++) {
            uint64_t startTime = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            for (int j = 0; j < DMA_BUFFER_COUNT; j++) {
                while(dmaCounts->hwReaderCountTotal <= swReaderCount);
                memcpy(readerBuffer + ((swReaderCount % DMA_BUFFER_COUNT) * DMA_BUFFER_SIZE),
                       tmpBuffer + ((swReaderCount % DMA_BUFFER_COUNT) * DMA_BUFFER_SIZE),
                       DMA_BUFFER_SIZE);
                swReaderCount += 1;
            }
            uint64_t endTime = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            avgTime += (endTime - startTime);
        }
        avgTime /= trials;
        double readerRate = readerSize / (avgTime / 1'000'000'000.0f);

        avgTime = 0;
        uint64_t swWriterCount = dmaCounts->hwWriterCountTotal;
        for (int i = 0; i < trials; i++) {
            uint64_t startTime = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            for (int j = 0; j < DMA_BUFFER_COUNT; j++) {
                while(dmaCounts->hwWriterCountTotal <= swWriterCount);
                memcmp(writerBuffer + ((swWriterCount % DMA_BUFFER_COUNT) * DMA_BUFFER_SIZE),
                       tmpBuffer + ((swWriterCount % DMA_BUFFER_COUNT) * DMA_BUFFER_SIZE),
                       DMA_BUFFER_SIZE);
                swWriterCount += 1;
            }
            uint64_t endTime = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            avgTime += (endTime - startTime);
        }
        avgTime /= trials;
        double writerRate = writerSize / (avgTime / 1'000'000'000.0f);

        printf("%0.4fms @ %0.4f MB/s to for buffer loopback\n", avgTime / 1'000'000.0f, ((readerRate + writerRate) / 1'000'000));
    }

    IOConnectUnmapMemory(connection, LITEPCIE_DMA_READER | 0, mach_task_self(), readerAddress);
    IOConnectUnmapMemory(connection, LITEPCIE_DMA_WRITER | 0, mach_task_self(), writerAddress);
    IOConnectUnmapMemory(connection, LITEPCIE_DMA_COUNTS | 0, mach_task_self(), countAddress);
    
    config_reader_dma(connection, 0, false);
    config_writer_dma(connection, 0, false);

    printf("Exiting");

    return EXIT_SUCCESS;
}
