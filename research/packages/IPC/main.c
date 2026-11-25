// Compile: gcc main.c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <semaphore.h>

#define SHM_PATH "/dev/shm/VeneerRuntime"
#define STR_SIZE 256

typedef struct {
    uint8_t flag;
    int64_t input[256];
    int64_t output[256];
} SharedData;

int main() {

    sem_t* semClient = sem_open("/VeneerRuntimeClient", O_CREAT, 0666, 0);
    if (semClient == SEM_FAILED) {
        perror("sem_open failed");
        return 1;
    }

    sem_t* semReceiver = sem_open("/VeneerRuntimeReceiver", O_CREAT, 0666, 1);
    if (semReceiver == SEM_FAILED) {
        perror("sem_open failed");
        return 1;
    }

    
    int fd = open(SHM_PATH, O_RDWR);
    if (fd == -1) {
        perror("Failed to open shared memory file");
        return 1;
    }

    sem_wait(semClient);
    SharedData* mapped_file = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }
    
    while (1) {
        if (mapped_file->flag == 1) {
            break;
        }

        for (int i = 0; i < sizeof(mapped_file->input)/sizeof(uint64_t); i++) {
            mapped_file->output[i] = mapped_file->input[i] * i; 
        }

        sem_post(semReceiver);
        sem_wait(semClient);

    }
   
    if (munmap(mapped_file, sizeof(SharedData)) == -1) {
        perror("munmap failed");
        close(fd);
        return 1;
    }
    sem_close(semReceiver);
    sem_close(semClient);
    close(fd);

    return 0;
}