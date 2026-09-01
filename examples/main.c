#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#define SHM_PATH "/dev/shm/VeneerRuntime"
#define MEM_SIZE 1024

int main() {
    // Open the shared memory file created by C#
    int fd = open(SHM_PATH, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open shared memory file");
        return 1;
    }

    // Map the file into the process memory space
    void* pBuf = mmap(NULL, MEM_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (pBuf == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    // Read the message length from the first 4 bytes
    int dataLength = *((int*)pBuf);
    
    // Read the string message right after the 4-byte integer boundary
    char* message = malloc(dataLength + 1);
    memcpy(message, (char*)pBuf + 4, dataLength);
    message[dataLength] = '\0'; // Null-terminate the string

    printf("Received from C#: %s\n", message);

    // Clean up resources
    free(message);
    munmap(pBuf, MEM_SIZE);
    close(fd);

    return 0;
}
