#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>

using namespace std;

#pragma pack(push,1)
// Partition entry structure (16 bytes each, 4 partitions)
struct PartitionEntry {
    uint8_t status;
    uint8_t firstCHS[3];
    uint8_t partitionType;
    uint8_t lastCHS[3];
    uint32_t firstLBASector;
    uint32_t sectorCount;
};
#pragma pack(pop)

// MBR structure (contains 4 partitions)
struct MBR {
    char bootstrap[446];
    PartitionEntry partitions[4];
    uint16_t signature;
};

struct Partition {
    int fd;                   // File descriptor for VDI
    size_t cursor;            // Current cursor within partition
    uint64_t partitionStart;  // Start of partition (bytes)
    uint64_t partitionSize;   // Size of partition (bytes)
};

// Function to clearly open a partition
Partition* openPartition(const char* filename, int partNum) {
    Partition* p = new Partition;
    p->fd = open(filename, O_RDWR);
    if (p->fd < 0) {
        perror("Failed to open VDI file");
        delete p;
        return nullptr;
    }

    MBR mbr;
    pread(p->fd, &mbr, sizeof(MBR), 0);

    if (partNum < 0 || partNum > 3 || mbr.partitions[partNum].sectorCount == 0) {
        cerr << "Invalid or empty partition number.\n";
        close(p->fd);
        delete p;
        return nullptr;
    }

    p->partitionStart = mbr.partitions[partNum].firstLBASector * 512;
    p->partitionSize = mbr.partitions[partNum].sectorCount * 512;
    p->cursor = 0;

    return p;
}

// Close the partition clearly
void closePartition(Partition* p) {
    if (p) {
        close(p->fd);
        delete p;
    }
}

// Read from partition clearly
ssize_t readPartition(Partition* p, void* buf, size_t count) {
    if (p->cursor + count > p->partitionSize)
        count = p->partitionSize - p->cursor; // Avoid going past partition

    ssize_t bytesRead = pread(p->fd, buf, count, p->partitionStart + p->cursor);
    if (bytesRead > 0) p->cursor += bytesRead;
    return bytesRead;
}

// Write to partition clearly
ssize_t writePartition(Partition* p, void* buf, size_t count) {
    if (p->cursor + count > p->partitionSize)
        count = p->partitionSize - p->cursor; // Avoid going past partition

    ssize_t bytesWritten = pwrite(p->fd, buf, count, p->partitionStart + p->cursor);
    if (bytesWritten > 0) p->cursor += bytesWritten;
    return bytesWritten;
}

// Move cursor clearly
off_t seekPartition(Partition* p, off_t offset, int anchor) {
    off_t newCursor;
    if (anchor == SEEK_SET) newCursor = offset;
    else if (anchor == SEEK_CUR) newCursor = p->cursor + offset;
    else if (anchor == SEEK_END) newCursor = p->partitionSize + offset;
    else {
        cerr << "Invalid anchor.\n";
        return -1;
    }

    if (newCursor >= 0 && newCursor <= p->partitionSize) {
        p->cursor = newCursor;
        return p->cursor;
    }

    cerr << "Seek out of partition bounds.\n";
    return -1;
}

// Display partition info clearly
void displayPartitionInfo(PartitionEntry &entry, int number) {
    cout << "Partition table entry " << number << ":\n";
    cout << "  Status: " << ((entry.status == 0x80) ? "Active" : "Inactive") << "\n";
    cout << "  Partition type: " << hex << (int)entry.partitionType << dec << "\n";
    cout << "  First LBA sector: " << entry.firstLBASector << "\n";
    cout << "  Sector count: " << entry.sectorCount << "\n\n";
}

// Main function clearly demonstrating your functions
int main() {
    Partition* partition = openPartition("../Step1/test.vdi", 0);
    if (!partition) return 1;

    // Display partition info
    MBR mbr;
    pread(partition->fd, &mbr, sizeof(MBR), 0);
    for (int i = 0; i < 4; i++)
        displayPartitionInfo(mbr.partitions[i], i);

    // Read first 1024 bytes from partition
    char buf[1024];
    if (readPartition(partition, buf, 1024) > 0)
        cout << "Successfully read 1024 bytes from partition.\n";
    else
        cerr << "Error reading from partition.\n";

    closePartition(partition);
    return 0;
}

