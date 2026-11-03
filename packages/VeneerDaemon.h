#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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

typedef struct {
    uint8_t type;
    size_t size;
} DataHeader;

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
            return sizeof(int64_t);
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
        total += sizeof(DataHeader) + typeSize * typeCount;
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
        total += sizeof(DataHeader) + typeSize * typeCount;
    }
    return total;
}

size_t getMappedFileSize(Schema schema, int isInput) {
    return (isInput ? getInputSize(schema) : getOutputSize(schema)) + sizeof(StreamHeader);
}

int allocateMappedFile(const char* channel, int isInput) {
    Schema schema = getSchema(channel);
    size_t size = getMappedFileSize(schema, isInput);

    char* pathIO = isInput ? "Input" : "Output";

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/%s", channel, pathIO);

    int fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
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

int writeData(const char* channel, int isInput, uint8_t type, uint8_t* bytes, size_t size) {
    
    char* pathIO = isInput ? "Input" : "Output";

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/%s", channel, pathIO);

    int fd = open(filepath, O_RDWR);
    if (fd == -1) {
        perror("Failed to open file");
        return -1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Failed to get file status");
        close(fd);
        return -1;
    }

    size_t filesize = (size_t) sb.st_size;

    void* mapped_file = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("Mapping failed");
        close(fd);
        return -1;
    }

    StreamHeader* header = (StreamHeader*) mapped_file;
    size_t offset = header->offset;

    uint8_t* pointer = (uint8_t*) header + offset;

    if (offset + sizeof(DataHeader) + (sizeof(u_int8_t) * size) > filesize) {
        perror("Map buffer can not receive this large of data");
        if (munmap(mapped_file, filesize) == -1) {
            perror("Failed to unmap file");
        }
        close(fd);
        return -1;
    }

    DataHeader* dataHeader = (DataHeader*) pointer;
    dataHeader->type = type;
    dataHeader->size = size;

    pointer = (uint8_t*) dataHeader + sizeof(DataHeader);
    memcpy(pointer, bytes, size);

    header->offset = offset + sizeof(DataHeader) + (size * sizeof(uint8_t));

    msync(mapped_file, filesize, MS_SYNC);

    if (munmap(mapped_file, filesize) == -1) {
        perror("Failed to unmap file");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}

int writeBytes(const char* channel, int isInput, uint8_t* bytes, size_t size) {
    return writeData(channel, isInput, 1, bytes, size * sizeof(uint8_t));
}

int writeChars(const char* channel, int isInput, char* chars, size_t size) {
    return writeData(channel, isInput, 2, (uint8_t *) chars, size * sizeof(char));
}

int writeInts(const char* channel, int isInput, char* ints, size_t size) {
    return writeData(channel, isInput, 3, (uint8_t *) ints, size * sizeof(int));
}

int writeLongs(const char* channel, int isInput, int64_t* longs, size_t size) {
    return writeData(channel, isInput, 4, (uint8_t *) longs, size * sizeof(int64_t));
}

int writeFloats(const char* channel, int isInput, float* floats, size_t size) {
    return writeData(channel, isInput, 5, (uint8_t *) floats, size * sizeof(float));
}

int writeDoubles(const char* channel, int isInput, double* doubles, size_t size) {
    return writeData(channel, isInput, 6, (uint8_t *) doubles, size * sizeof(double));
}

int flushStream(const char* channel, int isInput) {

    Schema schema = getSchema(channel);

    char* pathIO = isInput ? "Input" : "Output";

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/%s", channel, pathIO);

    int fd = open(filepath, O_RDWR);
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

    void *mapped_file = mmap(NULL, sb.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("Failed to map file");
        close(fd);
        return -1;
    }

    StreamHeader* header = (StreamHeader*) mapped_file;
    header->offset = sizeof(StreamHeader);

    uint8_t* pointer = (uint8_t*) header + sizeof(StreamHeader);

    if (isInput) {
        for (int i = 0; i < schema.header->inputByteSize; i++) {
            DataHeader* dataHeader = (DataHeader*) pointer;
            if (dataHeader->type != schema.inputBytes[i]) {
                perror("Data Types do not match");
                if (munmap(mapped_file, sb.st_size) == -1) {
                    perror("Failed to unmap file");
                    close(fd);
                    return -1;
                }

                if (munmap(schema.header, schema.size) == -1) {
                    perror("Failed to unmap file");
                    return -1;
                }

                close(fd);
                return -1;
            }

            size_t varCount = 1;
            for (int j = 0; j < schema.header->inputMatrixCols; j++) {
                varCount *= schema.inputMatrix[i * schema.header->inputMatrixCols + j];
            }

            if (dataHeader->size != varCount * getSizeofType(dataHeader->type)) {
                perror("Size does not match");
                if (munmap(mapped_file, sb.st_size) == -1) {
                    perror("Failed to unmap file");
                    close(fd);
                    return -1;
                }

                if (munmap(schema.header, schema.size) == -1) {
                    perror("Failed to unmap file");
                    return -1;
                }

                close(fd);
                return -1;
            }

            pointer += sizeof(DataHeader) + (getSizeofType(dataHeader->type) + dataHeader->size);
        }
    } else {
        for (int i = 0; i < schema.header->outputByteSize; i++) {
            DataHeader* dataHeader = (DataHeader*) pointer;
            if (dataHeader->type != schema.outputBytes[i]) {
                perror("Data Types do not match");
                if (munmap(mapped_file, sb.st_size) == -1) {
                    perror("Failed to unmap file");
                    close(fd);
                    return -1;
                }

                if (munmap(schema.header, schema.size) == -1) {
                    perror("Failed to unmap file");
                    return -1;
                }

                close(fd);
                return -1;
            }

            size_t varCount = 1;
            for (int j = 0; j < schema.header->outputMatrixCols; j++) {
                varCount *= schema.inputMatrix[i * schema.header->outputMatrixCols + j];
            }

            if (dataHeader->size != varCount * getSizeofType(dataHeader->type)) {
                perror("Size does not match");
                if (munmap(mapped_file, sb.st_size) == -1) {
                    perror("Failed to unmap file");
                    close(fd);
                    return -1;
                }

                if (munmap(schema.header, schema.size) == -1) {
                    perror("Failed to unmap file");
                    return -1;
                }

                close(fd);
                return -1;
            }

            pointer += sizeof(DataHeader) + (getSizeofType(dataHeader->type) + dataHeader->size);
        }
    }

    if (munmap(mapped_file, sb.st_size) == -1) {
        perror("Failed to unmap file");
        close(fd);
        return -1;
    }
    close(fd);

    if (munmap(schema.header, schema.size) == -1) {
        perror("Failed to unmap file");
        return -1;
    }

    return 0;
}

int readData(const char* channel, int isInput, uint8_t type, uint8_t* bytes, size_t size) {
    
    char* pathIO = isInput ? "Input" : "Output";

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/%s", channel, pathIO);

    int fd = open(filepath, O_RDWR);
    if (fd == -1) {
        perror("Failure to open file");
        return -1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Failure getting status of file");
        close(fd);
        return -1;
    }
    
    void* mapped_file = mmap(NULL, sb.st_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (mapped_file == MAP_FAILED) {
        perror("Failure mapping file");
        close(fd);
        return -1;
    }

    StreamHeader* header = (StreamHeader*) mapped_file;
    size_t offset = header->offset;

    uint8_t* pointer = (uint8_t*) header + offset;

    DataHeader* dataHeader = (DataHeader*) pointer;

    if (dataHeader->type != type || dataHeader->size != size) {
        perror("Type or size of data header doesn't match current parameters");
        if (munmap(mapped_file, sb.st_size) == -1) {
            perror("Failure to unmap file");
        }
        close(fd);
        return -1;
    }
    
    pointer += sizeof(DataHeader);

    for (int i = 0; i < size; i++) {
        bytes[i] = pointer[i]; 
    }

    header->offset = offset + sizeof(DataHeader) + dataHeader->size;

    if (munmap(mapped_file, sb.st_size) == -1) {
        perror("Failure to unmap file");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}

int readBytes(const char* channel, int isInput, uint8_t* bytes, size_t size) {
    return readData(channel, isInput, 1, bytes, size * sizeof(uint8_t));
}

int readChars(const char* channel, int isInput, char* chars, size_t size) {
    return readData(channel, isInput, 2, (uint8_t*) chars, size * sizeof(char));
}

int readInts(const char* channel, int isInput, int* ints, size_t size) {
    return readData(channel, isInput, 3, (uint8_t*) ints, size * sizeof(int));
}

int readLongs(const char* channel, int isInput, int64_t* longs, size_t size) {
    return readData(channel, isInput, 4, (uint8_t*) longs, size * sizeof(int64_t));
}

int readFloats(const char* channel, int isInput, float* floats, size_t size) {
    return readData(channel, isInput, 5, (uint8_t*) floats, size * sizeof(float));
}

int readDoubles(const char* channel, int isInput, double* doubles, size_t size) {
    return readData(channel, isInput, 6, (uint8_t*) doubles, size * sizeof(double));
}