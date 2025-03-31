#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <iomanip>
#include <cctype>
#include <cmath> 
#include <ctime>

// for std::ceil

// ------------------- 1) VDI Header Structures -------------------------
#pragma pack(push,1)
struct VDIHeader {
    uint8_t data[400];
};
#pragma pack(pop)

struct VDIFile {
    std::fstream file;

    // Info from the VDI header:
    uint32_t signature   = 0;   // at offset 0x40
    uint32_t imageType   = 0;   // at offset 0x4C
    uint32_t mapOffset   = 0;   // at offset 0x154
    uint32_t frameOffset = 0;   // at offset 0x158
    uint32_t frameSize   = 0;   // at offset 0x15C
    uint64_t diskSize    = 0;   // at offset 0x170
};

// ------------------- 2) MBR Partition Structures ----------------------
#pragma pack(push,1)
struct PartitionEntry {
    uint8_t  status;       // 0x80=active, 0x00=inactive
    uint8_t  firstCHS[3];
    uint8_t  type;         // e.g., 0x83 => Linux
    uint8_t  lastCHS[3];
    uint32_t firstLBA;
    uint32_t sectorCount;
};
#pragma pack(pop)

struct MBRPartition {
    VDIFile*        vdi;
    PartitionEntry  parts[4];
    uint64_t        startByte; // start of this partition in “virtual disk” bytes
    uint64_t        sizeBytes; // total size in bytes
    uint64_t        cursor;    // read pointer
};

// ------------------- 3) Ext2 Structures for Step 3 --------------------
#pragma pack(push,1)
struct Ext2Superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;  
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;           
    uint32_t s_wtime;           
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;           // should be 0xEF53
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
    
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_padding1;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_reserved_char_pad;
    uint16_t s_reserved_word_pad;
    uint32_t s_default_mount_options;
    uint32_t s_first_meta_bg;
    };
#pragma pack(pop)

#pragma pack(push,1)
struct Ext2BlockGroupDescriptor {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};
#pragma pack(pop)

// This structure holds all data for “Step 3”
struct Ext2File {
    MBRPartition* part;        // link to partition
    Ext2Superblock sb;         // main superblock
    std::vector<Ext2BlockGroupDescriptor> bgdt; // BG descriptors

    uint32_t blockSize;        // 1024 << s_log_block_size
    uint32_t numBlockGroups;   // # of block groups
};

// ------------------- 4) VDI read logic (from your code) ---------------
int64_t vdiRead(VDIFile &vdi, uint64_t diskOffset, void *buf, size_t count)
{
    if(diskOffset >= vdi.diskSize){
        return 0; // no data
    }
    uint64_t remain = vdi.diskSize - diskOffset;
    size_t toRead   = (count > remain)? (size_t)remain : count;

    uint64_t physical = (uint64_t)vdi.frameOffset + diskOffset;

    vdi.file.seekg((std::streamoff)physical, std::ios::beg);
    if(!vdi.file.good()){
        return -1;
    }
    vdi.file.read(reinterpret_cast<char*>(buf), toRead);
    return vdi.file.gcount(); 
}

bool vdiOpen(VDIFile &vdi, const std::string &filename)
{
    vdi.file.open(filename, std::ios::in | std::ios::binary);
    if(!vdi.file.is_open()){
        std::cerr <<"Could not open VDI file '"<<filename<<"'\n";
        return false;
    }
    // read first 400 bytes
    VDIHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    vdi.file.seekg(0, std::ios::beg);
    vdi.file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if(!vdi.file.good()){
        std::cerr <<"Error reading VDI header.\n";
        return false;
    }

    vdi.signature   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x40]);
    vdi.imageType   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x4C]);
    vdi.mapOffset   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x154]);
    vdi.frameOffset = *reinterpret_cast<const uint32_t*>(&hdr.data[0x158]);
    vdi.frameSize   = *reinterpret_cast<const uint32_t*>(&hdr.data[0x15C]);
    vdi.diskSize    = *reinterpret_cast<const uint64_t*>(&hdr.data[0x170]);

    // Optional debug: show some bytes
    std::cout <<"\n[DEBUG] Bytes at 0x150..0x15F:\n  ";
    for(int i=0x150; i<=0x15F; i++){
        std::cout<< std::hex << std::setw(2)<<std::setfill('0')
                 << (unsigned)hdr.data[i] <<" ";
    }
    std::cout <<"\n\n";

    std::cout<<"[DEBUG] VDI signature: 0x"<<std::hex<<vdi.signature<<std::dec<<"\n";
    std::cout<<"[DEBUG] VDI imageType: 0x"<<std::hex<<vdi.imageType<<std::dec<<"\n";
    std::cout<<"[DEBUG] mapOffset: 0x"<<std::hex<<vdi.mapOffset<<std::dec<<"\n";
    std::cout<<"[DEBUG] frameOffset: 0x"<<std::hex<<vdi.frameOffset<<std::dec<<"\n";
    std::cout<<"[DEBUG] frameSize: 0x"<<std::hex<<vdi.frameSize<<std::dec<<"\n";
    std::cout<<"[DEBUG] diskSize: 0x"<<std::hex<<vdi.diskSize
             <<"  ("<<std::dec<<vdi.diskSize<<" bytes)\n\n";

    return true;
}

void vdiClose(VDIFile &vdi)
{
    if(vdi.file.is_open()){
        vdi.file.close();
    }
}

// ------------------- 5) MBR parse logic (unchanged) -------------------
static void decodeCHS(const uint8_t chs[3], int &C, int &H, int &S)
{
    H = chs[0];
    S = chs[1] & 0x3F;
    C = chs[2] + ((chs[1] & 0xC0) << 2);
}
static void parseMBR(const uint8_t sector[512], PartitionEntry out[4])
{
    for(int i=0; i<4; i++){
        size_t off = 446 + i*16;
        out[i].status       = sector[off + 0];
        out[i].firstCHS[0]  = sector[off + 1];
        out[i].firstCHS[1]  = sector[off + 2];
        out[i].firstCHS[2]  = sector[off + 3];
        out[i].type         = sector[off + 4];
        out[i].lastCHS[0]   = sector[off + 5];
        out[i].lastCHS[1]   = sector[off + 6];
        out[i].lastCHS[2]   = sector[off + 7];
        out[i].firstLBA     = *reinterpret_cast<const uint32_t*>(&sector[off+8]);
        out[i].sectorCount  = *reinterpret_cast<const uint32_t*>(&sector[off+12]);
    }
}
static void printPartitionEntry(const PartitionEntry &p, int idx)
{
    std::cout << "Partition table entry " << idx << ":\n";
    bool active = (p.status == 0x80);
    std::cout << "Status: " << (active ? "Active\n" : "Inactive\n");

    int c1,h1,s1;
    decodeCHS(p.firstCHS, c1,h1,s1);
    std::cout << "First sector CHS: " << c1 << "-" << h1 << "-" << s1 << "\n";

    int c2,h2,s2;
    decodeCHS(p.lastCHS, c2,h2,s2);
    std::cout << "Last sector CHS: " << c2 << "-" << h2 << "-" << s2 << "\n";

    std::cout << "Partition type: " 
              << std::hex << (unsigned)p.type << " ";
    if(p.type == 0x83)      std::cout <<"linux native\n";
    else if(p.type == 0x00) std::cout <<"empty\n";
    else                    std::cout <<"(other)\n";
    std::cout << std::dec;

    std::cout << "First LBA sector: " << p.firstLBA << "\n";
    std::cout << "LBA sector count: " << p.sectorCount << "\n\n";
}
static bool mbrOpen(MBRPartition &mp, VDIFile &vdi, int index)
{
    mp.vdi = &vdi;
    mp.cursor = 0;

    // read 512 bytes from offset=0
    uint8_t sector[512];
    int64_t got = vdiRead(vdi, 0, sector, 512);
    if(got < 512){
        std::cerr <<"Could not read MBR\n";
        return false;
    }
    parseMBR(sector, mp.parts);

    if(index<0 || index>3){
        std::cerr <<"Invalid partition index\n";
        return false;
    }
    auto &pe = mp.parts[index];
    mp.startByte = (uint64_t)pe.firstLBA * 512ULL;
    mp.sizeBytes = (uint64_t)pe.sectorCount * 512ULL;

    return true;
}

// read from partition using mp.cursor
static int64_t mbrRead(MBRPartition &mp, void *buf, size_t count)
{
    if(mp.cursor >= mp.sizeBytes){
        return 0; 
    }
    uint64_t remain = mp.sizeBytes - mp.cursor;
    size_t toRead = (count > remain) ? (size_t)remain : count;

    uint64_t diskOffset = mp.startByte + mp.cursor;
    int64_t got = vdiRead(*mp.vdi, diskOffset, buf, toRead);
    if(got>0){
        mp.cursor += got;
    }
    return got;
}
static bool mbrSeek(MBRPartition &mp, int64_t offset)
{
    if(offset<0 || (uint64_t)offset>mp.sizeBytes){
        return false;
    }
    mp.cursor = offset;
    return true;
}

// ------------------- 6) Step 3: ext2 read block + superblock + BGDT ---
bool ext2ReadBlock(Ext2File &ext2, uint32_t blockIndex, void *buf)
{
    // offset in partition:
    uint64_t offset = (uint64_t)blockIndex * ext2.blockSize;
    if(offset + ext2.blockSize > ext2.part->sizeBytes) {
        std::memset(buf, 0, ext2.blockSize);
        return false;
    }
    uint64_t diskOffset = ext2.part->startByte + offset;
    // we can do direct read from vdi since we know exact offset
    VDIFile &vdi = *ext2.part->vdi;
    if(diskOffset + ext2.blockSize > vdi.diskSize) {
        std::memset(buf, 0, ext2.blockSize);
        return false;
    }
    vdi.file.seekg((std::streamoff)(vdi.frameOffset + diskOffset), std::ios::beg);
    if(!vdi.file.good()) {
        std::memset(buf, 0, ext2.blockSize);
        return false;
    }
    vdi.file.read(reinterpret_cast<char*>(buf), ext2.blockSize);
    if(vdi.file.gcount() < (std::streamsize)ext2.blockSize) {
        std::memset(buf, 0, ext2.blockSize);
        return false;
    }
    return true;
}

bool ext2LoadSuperblock(Ext2File &ext2)
{
    // Always at offset 1024 from the start of the partition
    if(1024 + 1024 > ext2.part->sizeBytes) {
        std::cerr<<"Partition too small for superblock\n";
        return false;
    }
    VDIFile &vdi = *ext2.part->vdi;
    uint64_t diskOffset = ext2.part->startByte + 1024ULL;
    if(diskOffset + 1024 > vdi.diskSize) {
        std::cerr<<"Disk too small for superblock\n";
        return false;
    }
    uint8_t buf[1024];
    std::memset(buf,0,sizeof(buf));
    vdi.file.seekg((std::streamoff)(vdi.frameOffset + diskOffset), std::ios::beg);
    if(!vdi.file.good()){
        std::cerr<<"Error seeking superblock\n";
        return false;
    }
    vdi.file.read(reinterpret_cast<char*>(buf), 1024);
    if(vdi.file.gcount()<1024){
        std::cerr<<"Partial read superblock\n";
        return false;
    }
    // copy
    std::memcpy(&ext2.sb, buf, sizeof(Ext2Superblock));
    if(ext2.sb.s_magic != 0xEF53) {
        std::cerr<<"Not a valid ext2 fs (magic=0x"<<std::hex<<ext2.sb.s_magic<<")\n";
        return false;
    }
    // compute blockSize
    ext2.blockSize = 1024U << ext2.sb.s_log_block_size;
    // compute number of block groups
    double n = double(ext2.sb.s_blocks_count) / double(ext2.sb.s_blocks_per_group);
    ext2.numBlockGroups = (uint32_t)std::ceil(n);
    return true;
}

bool ext2LoadBGDT(Ext2File &ext2)
{
    // The block group descriptor table starts at block (s_first_data_block+1).
    uint32_t bgdtBlock = ext2.sb.s_first_data_block + 1;

    // each descriptor is 32 bytes, total desc = numBlockGroups*32
    size_t totalBytes = ext2.numBlockGroups * sizeof(Ext2BlockGroupDescriptor);
    size_t blocksNeeded = (totalBytes + ext2.blockSize - 1) / ext2.blockSize;

    ext2.bgdt.resize(ext2.numBlockGroups);

    std::vector<uint8_t> blockBuf(ext2.blockSize,0);
    size_t bytesCopied=0;
    for(size_t b=0; b<blocksNeeded; b++){
        if(!ext2ReadBlock(ext2, bgdtBlock+(uint32_t)b, blockBuf.data())){
            std::cerr<<"Error reading BGDT block\n";
            return false;
        }
        size_t toCopy = ext2.blockSize;
        if(bytesCopied+toCopy> totalBytes){
            toCopy= totalBytes - bytesCopied;
        }
        std::memcpy( reinterpret_cast<uint8_t*>(ext2.bgdt.data()) + bytesCopied,
                     blockBuf.data(), toCopy );
        bytesCopied+= toCopy;
    }
    return true;
}

bool ext2Open(Ext2File &ext2, MBRPartition &part)
{
    ext2.part = &part;
    if(!ext2LoadSuperblock(ext2)){
        return false;
    }
    if(!ext2LoadBGDT(ext2)){
        return false;
    }
    return true;
}
void ext2Close(Ext2File & /*ext2*/)
{
    // vector will free automatically, no dynamic alloc
}

// -------------- 7) Printing debug info for Step 3 ---------------------
static void printSuperblock(const Ext2Superblock &sb)
{
    std::cout<<"Superblock contents:\n";
    std::cout<<"  s_inodes_count:       "<<sb.s_inodes_count<<"\n";
    std::cout<<"  s_blocks_count:       "<<sb.s_blocks_count<<"\n";
    std::cout<<"  s_r_blocks_count:     "<<sb.s_r_blocks_count<<"\n";
    std::cout<<"  s_free_blocks_count:  "<<sb.s_free_blocks_count<<"\n";
    std::cout<<"  s_free_inodes_count:  "<<sb.s_free_inodes_count<<"\n";
    std::cout<<"  s_first_data_block:   "<<sb.s_first_data_block<<"\n";
    std::cout<<"  s_log_block_size:     "<<sb.s_log_block_size
             <<" => blockSize="<<(1024U<<sb.s_log_block_size)<<"\n";
    std::cout<<"  s_blocks_per_group:   "<<sb.s_blocks_per_group<<"\n";
    std::cout<<"  s_inodes_per_group:   "<<sb.s_inodes_per_group<<"\n";
    std::cout<<"  s_magic:              0x"<<std::hex<<sb.s_magic<<std::dec<<"\n\n";
}

static void printBGDT(const Ext2File &ext2)
{
    std::cout<<"Block Group Descriptor Table:\n";
    std::cout<<"  #Groups = "<<ext2.numBlockGroups<<"\n";
    std::cout<<"  index | block_bitmap | inode_bitmap | inode_table  | free_blks | free_inodes | used_dirs\n";

    for(uint32_t i=0; i<ext2.numBlockGroups; i++){
        const Ext2BlockGroupDescriptor &bg = ext2.bgdt[i];
        std::cout<<"   "<<i
                 <<"       "<<bg.bg_block_bitmap
                 <<"          "<<bg.bg_inode_bitmap
                 <<"          "<<bg.bg_inode_table
                 <<"            "<<bg.bg_free_blocks_count
                 <<"            "<<bg.bg_free_inodes_count
                 <<"            "<<bg.bg_used_dirs_count<<"\n";
    }
    std::cout<<"\n";
}

// -------------- 8) Hex-dump helper  (unchanged) -----------------------
static void hexDump(const uint8_t *buf, size_t length, uint64_t offsetShown)
{
    size_t index = 0;
    while(index < length) {
        std::cout << "Offset:  0x" 
                  << std::hex << (offsetShown + index)
                  << std::dec << "\n";

        size_t chunkEnd = std::min(index + 256, length);
        while(index < chunkEnd) {
            size_t lineCount = std::min((size_t)16, chunkEnd - index);
            std::cout << std::setw(2) << std::setfill('0')
                      << std::hex << (index & 0xff) << "|";
            for(size_t j=0; j<lineCount; j++){
                std::cout <<" "<<std::setw(2)<<(unsigned)buf[index+j];
            }
            if(lineCount < 16){
                for(size_t pad=0; pad<(16 - lineCount); pad++){
                    std::cout<<"   ";
                }
            }
            std::cout<<" |";
            for(size_t j=0; j<lineCount; j++){
                char c=(char)buf[index+j];
                if(std::isprint((unsigned char)c)) std::cout<<c; else std::cout<<".";
            }
            if(lineCount<16){
                for(size_t pad=0; pad<(16-lineCount); pad++){
                    std::cout<<" ";
                }
            }
            std::cout<<"|\n";
            index+= lineCount;
        }
    }
}
void printExtendedSuperblockInfo(const Ext2Superblock& sb) {
    auto printTime = [](const char* label, uint32_t timestamp) {
        time_t t = timestamp;
        std::cout << label << ": " << std::asctime(std::localtime(&t));
    };

    std::cout << "Log fragment size: " << sb.s_log_frag_size << " ("
              << (1024 << sb.s_log_frag_size) << ")\n";
    std::cout << "Fragments per group: " << sb.s_frags_per_group << "\n";
    printTime("Last mount time", sb.s_mtime);
    printTime("Last write time", sb.s_wtime);
    std::cout << "Mount count: " << sb.s_mnt_count << "\n";
    std::cout << "Max mount count: " << sb.s_max_mnt_count << "\n";
    std::cout << "State: " << sb.s_state << "\n";
    std::cout << "Error processing: " << sb.s_errors << "\n";
    std::cout << "Revision level: " << sb.s_rev_level << "\n";
    printTime("Last system check", sb.s_lastcheck);
    std::cout << "Check interval: " << sb.s_checkinterval << "\n";
    std::cout << "OS creator: " << sb.s_creator_os << "\n";
    std::cout << "Default reserve UID: " << sb.s_def_resuid << "\n";
    std::cout << "Default reserve GID: " << sb.s_def_resgid << "\n";
}

// -------------- 9) Main function for Step 3 demonstration ------------
int main(int argc, char* argv[])
{
    if(argc<2){
        std::cerr <<"Usage: "<<argv[0]<<" <VDI file>\n";
        return 1;
    }
    // 1) Open the VDI
    VDIFile vdi;
    if(!vdiOpen(vdi, argv[1])){
        return 1;
    }

    // 2) Open MBR partition #0
    MBRPartition mp;
    if(!mbrOpen(mp, vdi, 0)){
        vdiClose(vdi);
        return 1;
    }

    // 3) Print partition entries (just for info)
    for(int i=0; i<4; i++){
        printPartitionEntry(mp.parts[i], i);
    }

    // 4) Create Ext2File, load superblock & BGDT
    Ext2File ext2;
    if(!ext2Open(ext2, mp)){
        std::cerr<<"Failed to open ext2 from partition #0\n";
        vdiClose(vdi);
        return 1;
    }

    // 5) Print superblock fields
    printSuperblock(ext2.sb);
    std::cout << "\nAdditional superblock info:\n";
    printExtendedSuperblockInfo(ext2.sb);
    
    // 6) Print BGDT
    printBGDT(ext2);

    // 7) (Optional) show hex dump of superblock from offset 1024 in the partition
    if(mbrSeek(mp, 1024)) {
        std::vector<uint8_t> sbuf(1024,0);
        int64_t got = mbrRead(mp, sbuf.data(), sbuf.size());
        if(got>0){
            std::cout<<"Hex dump of superblock (like logs show):\n";
            hexDump(sbuf.data(), (size_t)got, 0x400);
        }
    }

    ext2Close(ext2);
    vdiClose(vdi);
    return 0;
}
