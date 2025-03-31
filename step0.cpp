#include <iostream>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace std;

// Display up to 256 bytes clearly in hexadecimal and character format
void displayBufferPage(uint8_t *buf, uint32_t count, uint32_t skip, uint64_t offset) {
    printf("Offset: 0x%lx\n", offset);
    printf("   ");
    for (int i = 0; i < 16; i++) printf(" %02x ", i);
    printf("\n  +-------------------------------------------------+\n");

    for (uint32_t i = 0; i < 256; i += 16) {
        printf("%02x|", i);
        for (uint32_t j = 0; j < 16; j++) {
            uint32_t pos = i + j;
            if (pos >= skip && pos < (skip + count))
                printf("%02x  ", buf[pos]);
            else
                printf("    ");
        }
        printf("|");
        for (uint32_t j = 0; j < 16; j++) {
            uint32_t pos = i + j;
            char c = (pos >= skip && pos < (skip + count) && isprint(buf[pos])) ? buf[pos] : ' ';
            printf("%c", c);
        }
        printf("|\n");
    }
    printf("  +-------------------------------------------------+\n");
}

// Display full buffer by repeatedly calling displayBufferPage
void displayBuffer(uint8_t *buf, uint32_t count, uint64_t offset) {
    uint32_t displayed = 0;
    while (displayed < count) {
        uint32_t bytesToShow = (count - displayed > 256) ? 256 : (count - displayed);
        displayBufferPage(buf + displayed, bytesToShow, 0, offset + displayed);
        displayed += bytesToShow;
    }
}

// Main function to demonstrate file access functions
int main() {
    // Open file example (change "example.txt" to any available file)
    int fd = open("example.txt", O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return 1;
    }

    // Read file content into buffer
    uint8_t buffer[512];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
    if (bytesRead < 0) {
        perror("read failed");
        close(fd);
        return 1;
    }

    // Display buffer content
    displayBuffer(buffer, bytesRead, 0);

    // Close file
    close(fd);

    return 0;
}

