#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>

using namespace std;

#pragma pack(push,1)
// Simplified VDI Header structure (partial)
struct VDIHeader {
    char imageName[64];
    uint32_t signature;
    uint32_t version;
    uint32_t headerSize;
    uint32_t imageType;
    uint32_t imageFlags;
    char description[256];
    uint32_t offsetBlocks;
    uint32_t offsetData;
    uint32_t sectorSize;
    uint32_t unused;
    uint64_t diskSize;
    uint32_t blockSize;
    uint32_t blockExtraSize;
    uint32_t totalBlocks;
    uint32_t blocksAllocated;
    // More fields exist, but these are the most important ones
};
#pragma pack(pop)

// Structure to store VDI file data
struct VDIFile {
    int fd;
    size_t cursor;
    VDIHeader header;
};

// Function to clearly open and load VDI file
VDIFile* vdiOpen(const char *filename) {
    VDIFile* vdi = new VDIFile;
    vdi->fd = open(filename, O_RDWR);
    if (vdi->fd < 0) {
        perror("Failed to open VDI file");
        delete vdi;
        return nullptr;
    }
    vdi->cursor = 0;

    // Load header clearly
    pread(vdi->fd, &vdi->header, sizeof(VDIHeader), 0);
    return vdi;
}

// Function to close VDI file
void vdiClose(VDIFile* vdi) {
    if (vdi) {
        close(vdi->fd);
        delete vdi;
    }
}

// Function to read from VDI file
ssize_t vdiRead(VDIFile* vdi, void* buf, size_t count) {
    ssize_t bytesRead = pread(vdi->fd, buf, count, vdi->header.offsetData + vdi->cursor);
    if (bytesRead > 0) vdi->cursor += bytesRead;
    return bytesRead;
}

// Function to write to VDI file
ssize_t vdiWrite(VDIFile* vdi, void* buf, size_t count) {
    ssize_t bytesWritten = pwrite(vdi->fd, buf, count, vdi->header.offsetData + vdi->cursor);
    if (bytesWritten > 0) vdi->cursor += bytesWritten;
    return bytesWritten;
}

// Function to move cursor
off_t vdiSeek(VDIFile* vdi, off_t offset, int anchor) {
    size_t newCursor;
    if (anchor == SEEK_SET) newCursor = offset;
    else if (anchor == SEEK_CUR) newCursor = vdi->cursor + offset;
    else {
        cerr << "Unsupported anchor type.\n";
        return -1;
    }
    if (newCursor <= vdi->header.diskSize) {
        vdi->cursor = newCursor;
        return vdi->cursor;
    }
    cerr << "Seek out of range.\n";
    return -1;
}

// Clearly display the VDI header information
void displayHeader(VDIFile* vdi) {
    cout << "VDI Header Information:\n";
    cout << "Image name: " << vdi->header.imageName << "\n";
    cout << "Signature: " << hex << vdi->header.signature << "\n";
    cout << "Version: " << dec << vdi->header.version << "\n";
    cout << "Header Size: " << vdi->header.headerSize << "\n";
    cout << "Image Type: " << vdi->header.imageType << "\n";
    cout << "Disk Size: " << vdi->header.diskSize << " bytes\n";
    cout << "Block Size: " << vdi->header.blockSize << " bytes\n";
    cout << "Blocks Allocated: " << vdi->header.blocksAllocated << "\n";
}

// Main function clearly demonstrating all functions
int main() {
    VDIFile* vdi = vdiOpen("test.vdi");
    if (!vdi) {
        cerr << "Error: Could not open VDI file.\n";
        return 1;
    }

    displayHeader(vdi);  // Clearly display header

    // Test reading 512 bytes
    char buffer[512];
    if (vdiRead(vdi, buffer, sizeof(buffer)) > 0)
        cout << "Read 512 bytes successfully from VDI.\n";
    else
        cerr << "Error reading from VDI.\n";

    // Clearly test writing (write back what we read)
    vdiSeek(vdi, 0, SEEK_SET);  // Reset cursor to start
    if (vdiWrite(vdi, buffer, sizeof(buffer)) > 0)
        cout << "Wrote 512 bytes successfully to VDI.\n";
    else
        cerr << "Error writing to VDI.\n";

    vdiClose(vdi);
    return 0;
}

