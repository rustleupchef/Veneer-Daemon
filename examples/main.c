#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#define SHM_PATH "/dev/shm/VeneerRuntime"
#define STR_SIZE 256

// Use standard fixed-size layout matching C# memory structure
typedef struct {
    int flag;             // 0: Idle/Processed, 1: Ready for C, 2: Ready for C#
    char input[STR_SIZE];
    char output[STR_SIZE];
} SharedData;

int main() {
    // Open existing shared memory file with read-write access
    int fd = open(SHM_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open shared memory file");
        return 1;
    }

    // Map struct memory space with write access
    SharedData* shm = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }

    printf("C Service started listening on %s...\n", SHM_PATH);

    while (1) {
        // Wait until C# updates flag to 1
        if (shm->flag == 1) {
            printf("Received input: %s\n", shm->input);

            // Process data and populate output string
            snprintf(shm->output, STR_SIZE, "Processed by C: %s", shm->input);

            // Signal to C# that processing is complete
            shm->flag = 2;
        }

        // Sleep briefly to avoid 100% CPU utilization while polling
        usleep(10000); 
    }

    // Cleanup resources
    munmap(shm, sizeof(SharedData));
    close(fd);

    return 0;
}