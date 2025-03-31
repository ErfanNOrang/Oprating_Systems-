#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <iomanip>
#include <cctype>

#pragma pack(push, 1)
/// We'll read at least 400 bytes from the VDI header
struct VDIHeader {
    uint8_t data[400];
};
#pragma pack(pop)

struct VDIFile {
    std::fstream file;
    
    // Offsets we care about in “good-fixed” images
    uint32_t mapOffset   = 0;
    uint32_t frameOffset = 0;
    uint32_t frameSize   = 0;  // Must read from offset 0x15C
    uint64_t diskSize    = 0;

    // For debugging logs
    uint32_t signature   = 0;  // offset 0x40
    uint32_t imageType   = 0;  // offset 0x4C
};

#pragma pack(push,1)
struct PartitionEntry {
    uint8_t  status;       // 0x80=active, 0x00=inactive
    uint8_t  firstCHS[3];
    uint8_t  type;         // 0x83 => Linux, 0x00 => empty
    uint8_t  lastCHS[3];
    uint32_t firstLBA;     // offset 8..11
    uint32_t sectorCount;  // offset 12..15
};
#pragma pack(pop)

struct MBRPartition {
    VDIFile* vdi;
    PartitionEntry parts[4];

    uint64_t startByte;
    uint64_t sizeBytes;
    uint64_t cursor;
};

// -------------------------------------------------------------------
//                   VDI  Functions
// -------------------------------------------------------------------
bool vdiOpen(VDIFile &vdi, const std::string &filename)
{
    vdi.file.open(filename, std::ios::in | std::ios::binary);
    if (!vdi.file.is_open()) {
        std::cerr << "Error opening " << filename << "\n";
        return false;
    }

    // Read at least 400 bytes of the VDI header
    VDIHeader hdr{};
    vdi.file.seekg(0, std::ios::beg);
    vdi.file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!vdi.file.good()) {
        std::cerr << "Error reading VDI header.\n";
        return false;
    }

    // Known offsets for the v1 VDI “good-fixed” images:
    //   signature   @ 0x40  (4 bytes)
    //   imageType   @ 0x4C  (4 bytes)
    //   mapOffset   @ 0x154 (4 bytes)
    //   frameOffset @ 0x158 (4 bytes)
    //   frameSize   @ 0x15C (4 bytes)  <-- must fix here!
    //   diskSize    @ 0x170 (8 bytes)
    //
    // If you read from a different offset for frameSize, it stays 0.

    vdi.signature   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x40]);
    vdi.imageType   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x4C]);
    vdi.mapOffset   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x154]);
    vdi.frameOffset = *reinterpret_cast<const uint32_t*>(&hdr.data[0x158]);
    vdi.frameSize   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x15C]); // key fix
    vdi.diskSize    = *reinterpret_cast<const uint64_t*>(&hdr.data[0x170]);

    // Debug
    std::cout << "[DEBUG] VDI signature: 0x" << std::hex << vdi.signature << std::dec << "\n";
    std::cout << "[DEBUG] VDI imageType: 0x" << std::hex << vdi.imageType << std::dec << "\n";
    std::cout << "[DEBUG] mapOffset: 0x"   << std::hex << vdi.mapOffset << std::dec << "\n";
    std::cout << "[DEBUG] frameOffset: 0x"<< std::hex << vdi.frameOffset << std::dec << "\n";
    std::cout << "[DEBUG] frameSize: 0x"  << std::hex << vdi.frameSize << std::dec << "\n";
    std::cout << "[DEBUG] diskSize: 0x"   << std::hex << vdi.diskSize
              << "  (" << std::dec << vdi.diskSize << " bytes)\n\n";

    return true;
}

void vdiClose(VDIFile &vdi)
{
    if (vdi.file.is_open()) {
        vdi.file.close();
    }
}

// read from “virtual disk”:  physical offset = frameOffset + diskOffset
int64_t vdiRead(VDIFile &vdi, uint64_t diskOffset, void *buf, size_t count)
{
    // Check boundaries
    if (diskOffset >= vdi.diskSize) {
        return 0;
    }
    uint64_t remain = vdi.diskSize - diskOffset;
    size_t toRead   = (count > remain) ? (size_t)remain : count;

    uint64_t physical = (uint64_t)vdi.frameOffset + diskOffset;
    vdi.file.seekg((std::streamoff)physical, std::ios::beg);
    if (!vdi.file.good()) {
        return -1;
    }
    vdi.file.read(reinterpret_cast<char*>(buf), toRead);
    return vdi.file.gcount();
}

// -------------------------------------------------------------------
//                   MBR + Partition  Functions
// -------------------------------------------------------------------
void parseMBR(const uint8_t sector[512], PartitionEntry out[4])
{
    // The 4 entries are at offset 446..(446+16*4=510)
    for (int i = 0; i < 4; i++) {
        size_t off = 446 + i*16;
        out[i].status       = sector[off + 0];
        out[i].firstCHS[0]  = sector[off + 1];
        out[i].firstCHS[1]  = sector[off + 2];
        out[i].firstCHS[2]  = sector[off + 3];
        out[i].type         = sector[off + 4];
        out[i].lastCHS[0]   = sector[off + 5];
        out[i].lastCHS[1]   = sector[off + 6];
        out[i].lastCHS[2]   = sector[off + 7];
        out[i].firstLBA     = *reinterpret_cast<const uint32_t*>(&sector[off + 8]);
        out[i].sectorCount  = *reinterpret_cast<const uint32_t*>(&sector[off +12]);
    }
}

static void decodeCHS(const uint8_t chs[3], int &C, int &H, int &S)
{
    H = chs[0];
    S = chs[1] & 0x3F;
    C = chs[2] + ((chs[1] & 0xC0) << 2);
}

static void printPartitionEntry(const PartitionEntry &p, int index)
{
    std::cout << "Partition table entry " << index << ":\n";
    bool active = (p.status == 0x80);
    std::cout << "Status: " << (active ? "Active\n" : "Inactive\n");

    int c1, h1, s1;
    decodeCHS(p.firstCHS, c1, h1, s1);
    std::cout << "First sector CHS: " << c1 << "-" << h1 << "-" << s1 << "\n";

    int c2, h2, s2;
    decodeCHS(p.lastCHS, c2, h2, s2);
    std::cout << "Last sector CHS: " << c2 << "-" << h2 << "-" << s2 << "\n";

    // Print type in hex
    std::cout << std::hex << std::setw(2) << std::setfill('0');
    std::cout << "Partition type: " << (unsigned)p.type << " ";
    if (p.type == 0x83) std::cout << "linux native\n";
    else if (p.type == 0x00) std::cout << "empty\n";
    else std::cout << "(other)\n";
    std::cout << std::dec;

    std::cout << "First LBA sector: " << p.firstLBA << "\n";
    std::cout << "LBA sector count: " << p.sectorCount << "\n\n";
}

bool mbrOpen(MBRPartition &mp, VDIFile &vdi, int pIndex)
{
    std::vector<uint8_t> sector(512);
    int64_t got = vdiRead(vdi, 0, sector.data(), sector.size());
    if (got < 512) {
        std::cerr << "Error reading MBR from offset 0\n";
        return false;
    }
    parseMBR(sector.data(), mp.parts);

    if (pIndex < 0 || pIndex > 3) {
        std::cerr << "Partition index out of range\n";
        return false;
    }
    mp.vdi       = &vdi;
    mp.cursor    = 0;
    auto &pe     = mp.parts[pIndex];
    mp.startByte = (uint64_t)pe.firstLBA * 512ULL;
    mp.sizeBytes = (uint64_t)pe.sectorCount * 512ULL;
    return true;
}

int64_t mbrRead(MBRPartition &mp, void *buf, size_t count)
{
    if (mp.cursor >= mp.sizeBytes) {
        return 0;
    }
    uint64_t remain = mp.sizeBytes - mp.cursor;
    size_t toRead   = (count > remain) ? (size_t)remain : count;

    uint64_t diskOffset = mp.startByte + mp.cursor;
    int64_t got = vdiRead(*mp.vdi, diskOffset, buf, toRead);
    if (got > 0) {
        mp.cursor += got;
    }
    return got;
}

bool mbrSeek(MBRPartition &mp, int64_t offset)
{
    if (offset < 0 || (uint64_t)offset > mp.sizeBytes) {
        return false;
    }
    mp.cursor = (uint64_t)offset;
    return true;
}

// A quick hexdump in lines of 256 bytes max per "page"
static void hexDump(const uint8_t *buf, size_t length, uint64_t startOffset)
{
    const size_t bytesPerLine = 16;
    size_t i = 0;
    while (i < length) {
        std::cout << "Offset:  0x" << std::hex << (startOffset + i) 
                  << std::dec << "\n";
        size_t blockEnd = std::min(i + 256, length);
        while (i < blockEnd) {
            // show line offset mod 256 in hex
            std::cout << std::setw(2) << std::setfill('0') 
                      << std::hex << (i & 0xff) << "|";
            size_t lineEnd = std::min(i + bytesPerLine, blockEnd);
            for (size_t j = i; j < lineEnd; j++) {
                std::cout << " " << std::setw(2) << (unsigned)buf[j];
            }
            // If fewer than 16 bytes, pad
            size_t rowCount = lineEnd - i;
            if (rowCount < bytesPerLine) {
                for (size_t pad = 0; pad < (bytesPerLine - rowCount); pad++) {
                    std::cout << "   ";
                }
            }
            // ASCII chunk
            std::cout << " |";
            for (size_t j = i; j < lineEnd; j++) {
                unsigned char c = buf[j];
                if (std::isprint(c)) std::cout << c; else std::cout << '.';
            }
            if (rowCount < bytesPerLine) {
                for (size_t pad = 0; pad < (bytesPerLine - rowCount); pad++) {
                    std::cout << " ";
                }
            }
            std::cout << "|\n";
            i += rowCount;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <VDI file>\n";
        return 1;
    }

    VDIFile vdi;
    if (!vdiOpen(vdi, argv[1])) {
        return 1;
    }

    // We'll open partition #0 (the first partition) from the MBR
    MBRPartition mp;
    if (!mbrOpen(mp, vdi, 0)) {
        vdiClose(vdi);
        return 1;
    }

    // Print all 4 partition entries from the MBR
    for (int i = 0; i < 4; i++) {
        printPartitionEntry(mp.parts[i], i);
    }

    // Attempt to read the superblock at offset 1024 within the partition
    std::cout << "Superblock:\n";
    if (!mbrSeek(mp, 1024)) {
        std::cerr << "Cannot seek to offset 1024 in the partition\n";
    } else {
        std::vector<uint8_t> sbuf(1024);
        int64_t got = mbrRead(mp, sbuf.data(), sbuf.size());
        if (got <= 0) {
            std::cerr << "Read 0 bytes in superblock?\n";
        } else {
            hexDump(sbuf.data(), (size_t)got, /*displayOffset=*/0x400);
        }
    }

    vdiClose(vdi);
    return 0;
}
