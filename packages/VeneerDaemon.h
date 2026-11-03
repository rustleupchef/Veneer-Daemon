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

typedef struct {
    size_t offset;
} StreamHeader;

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
        return -1;
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
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/Schema", channel);
    
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

size_t getSizeofType(uint8_t type) {
    switch (type)
    {
        case 1:
            return sizeof(uint8_t);
        case 2:
            return sizeof(char);
        case 3:
            return sizeof(int);
        case 4:
            return sizeof(int16_t);
        case 5:
            return sizeof(float);
        case 6:
            return sizeof(double);
        default:
            return 0;
    }
}

size_t getInputSize(Schema schema) {
    size_t total = 0;
    for (int i = 0; i < schema.header->inputMatrixRows; i++) {
        size_t typeSize = getSizeofType(schema.inputBytes[i]);
        size_t typeCount = 1;
        for (int j = 0; j < schema.header->inputMatrixCols; j++) {
            typeCount *= schema.inputMatrix[schema.header->inputMatrixCols * i + j];
        }
        total += typeSize * typeCount;
    }
    return total;
}

size_t getOutputSize(Schema schema) {
    size_t total = 0;
    for (int i = 0; i < schema.header->outputMatrixRows; i++) {
        size_t typeSize = getSizeofType(schema.outputBytes[i]);
        size_t typeCount = 1;
        for (int j = 0; j < schema.header->outputMatrixCols; j++) {
            typeCount *= schema.outputMatrix[schema.header->outputMatrixCols * i + j];
        }
        total += typeSize * typeCount;
    }
    return total;
}

size_t getMappedFileSize(Schema schema, int isInput) {
    return isInput ? getInputSize(schema) : getOutputSize(schema);
}

int allocateMappedFile(const char* channel, int isInput) {
    Schema schema = getSchema(channel);
    size_t size = getMappedFileSize(schema, isInput);

    char* pathIO = isInput ? "Input" : "Output";

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/%s", channel, pathIO);

    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("Failed to open or create the file");
        return -1;
    }

    if (ftruncate(fd, (off_t)size) == -1) {
        perror("Failed to set file size");
        close(fd);
        return -1;
    }

    void* mapped_file = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("Failed initalizing mapped file");
        close(fd);
        return -1;
    }

    StreamHeader* header = (StreamHeader*) mapped_file;
    header->offset = sizeof(size_t);

    msync(mapped_file, size, MS_SYNC);
    
    if (munmap(mapped_file, size) == -1) {
        perror("Failed unmapping file");
        close(fd);
        return -1;
    }

    close(fd);

    if (munmap(schema.header, schema.size) == -1) {
        perror("Failed unmapping file");
        return -1;
    }

    return 0;
}