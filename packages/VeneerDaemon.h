#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#pragma pack(push, 1)
typedef struct {
    int64_t inputByteSize;
    int64_t outputByteSize;
    int64_t inputMatrixRows;
    int64_t inputMatrixCols;
    int64_t outputMatrixRows;
    int64_t outputMatrixCols;
} SchemaHeader;
#pragma pack(pop)

typedef struct {
    size_t size;
    SchemaHeader* header;
    uint8_t* inputBytes;
    uint8_t* outputBytes;
    int64_t* inputMatrix;
    int64_t* outputMatrix;

} Schema;

int getFlag(const char* channel) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/Flag", channel);
    
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open\n");
        return -1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Failed to get file size");
        close(fd);
        return 1;
    }

    void *mapped_file = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("Failed to map file");
        close(fd);
        return -1;
    }


    unsigned char *single_byte = (unsigned char *) mapped_file;
    uint8_t value = single_byte[0];

    if (munmap(mapped_file, sb.st_size) == -1) {
        perror("Error unmapping");
    }
    close(fd);

    return value;
}

Schema getSchema (const char* channel) {
    Schema schema = {0, NULL, NULL, NULL, NULL, NULL};

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/Flag", channel);
    
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open\n");
        return schema;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Failed to get file size");
        close(fd);
        return schema;
    }

    if (sb.st_size < sizeof(SchemaHeader)) {
        perror("File is too small to contain a valid header");
        close(fd);
        return schema;
    }

    void *mapped_file = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("Failed to map file");
        close(fd);
        return schema;
    }

    madvise(mapped_file, sb.st_size, MADV_SEQUENTIAL);


    schema.size = sb.st_size;
    
    schema.header = (SchemaHeader *)mapped_file;
    
    uint8_t *current_ptr = (uint8_t *) mapped_file + sizeof(SchemaHeader);
    
    schema.inputBytes = current_ptr;
    current_ptr += schema.header->inputByteSize;
    
    schema.outputBytes = current_ptr;
    current_ptr += schema.header->outputByteSize;

    schema.inputMatrix = (int64_t *) current_ptr;
    size_t inputMatrixTotalElements = schema.header->inputMatrixCols * schema.header->inputMatrixRows;
    current_ptr += inputMatrixTotalElements * sizeof(int64_t);
    
    schema.outputMatrix = (int64_t *) current_ptr;

    close(fd);

    return schema;
}