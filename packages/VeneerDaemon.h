#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>

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

typedef struct {
    void* map;
    size_t size;
} MappedFile;

typedef struct {
    const char* channel;
    MappedFile flag;
    MappedFile schema;
    MappedFile input;
    MappedFile output;
} MappedFiles;

#define EMPTY_MAPPED_FILE {.map = MAP_FAILED, .size = 0}

static MappedFiles connectionFiles = {
    .channel = "",
    .flag = EMPTY_MAPPED_FILE,
    .schema = EMPTY_MAPPED_FILE,
    .input = EMPTY_MAPPED_FILE,
    .output = EMPTY_MAPPED_FILE
};

Schema getSchema () {
    Schema schema = {NULL, NULL, NULL, NULL, NULL};

    schema.header = (SchemaHeader *) connectionFiles.schema.map;
    
    uint8_t *current_ptr = (uint8_t *) connectionFiles.schema.map + sizeof(SchemaHeader);
    
    schema.inputBytes = current_ptr;
    current_ptr += schema.header->inputByteSize;
    
    schema.outputBytes = current_ptr;
    current_ptr += schema.header->outputByteSize;

    schema.inputMatrix = (int64_t *) current_ptr;
    size_t inputMatrixTotalElements = schema.header->inputMatrixCols * schema.header->inputMatrixRows;
    current_ptr += inputMatrixTotalElements * sizeof(int64_t);
    
    schema.outputMatrix = (int64_t *) current_ptr;

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

MappedFile mapFile(const char* channel, const char* file, int isIO, int isInput) {
    MappedFile mmf = EMPTY_MAPPED_FILE;

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/dev/shm/%s/%s", channel, file);

    int fd = open(filepath, O_RDWR);
    if (fd == -1) {
        perror("Failed to open the file");
        return mmf;
    }


    size_t size;
    if (isIO) {
        size = getMappedFileSize(getSchema(), isInput);
    } else {
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            perror("Failed to get file status");
            close(fd);
            return mmf;
        }
        size = sb.st_size;
    }

    void* map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (map == MAP_FAILED) {
        perror("Failure to map file");
        close(fd);
        return mmf;
    }

    mmf.map = map;
    mmf.size = size;

    close(fd);

    return mmf;
}

void setFlagPointer() {
    if (connectionFiles.flag.map != MAP_FAILED) {
        return;
    }
    connectionFiles.flag = mapFile(connectionFiles.channel, "Flag", false, false);
}

void setSchemaPointer() {
    if (connectionFiles.schema.map != MAP_FAILED) {
        return;
    }
    connectionFiles.schema = mapFile(connectionFiles.channel, "Schema", false, false);
}

void setInputPointer() {
    if (connectionFiles.input.map != MAP_FAILED) {
        return;
    }
    connectionFiles.input = mapFile(connectionFiles.channel, "Input", true, true);
}

void setOutputPointer() {
    if (connectionFiles.output.map != MAP_FAILED) {
        return;
    }
    connectionFiles.output = mapFile(connectionFiles.channel, "Output", true, false);
}

void setPointers(const char* channel) {
    connectionFiles.channel = channel;
    setFlagPointer();
    setSchemaPointer();
    setInputPointer();
    setOutputPointer();
}

int unmapPointer(MappedFile mmf) {
    return munmap(mmf.map, mmf.size);
}

int unmapPointers() {
    int tally = 0;
    tally += unmapPointer(connectionFiles.flag);
    tally += unmapPointer(connectionFiles.schema);
    tally += unmapPointer(connectionFiles.input);
    tally += unmapPointer(connectionFiles.output);
    return tally < 0 ? -1 : 0;   
}

int getFlag() {

    if (connectionFiles.channel == NULL) {
        perror("Pointers have not been set");
        return -1;
    }
    setFlagPointer();

    unsigned char *single_byte = (unsigned char *) connectionFiles.flag.map;
    uint8_t value = single_byte[0];

    return value;
}

int setFlag(uint8_t value) {

    uint8_t* single_byte = (uint8_t*) connectionFiles.flag.map;
    single_byte[0] = value;
    
    return 0;
}

int writeData(int isInput, uint8_t type, uint8_t* bytes, size_t size) {

    MappedFile mmf = isInput ? connectionFiles.input : connectionFiles.output;

    StreamHeader* header = (StreamHeader*) mmf.map;
    size_t offset = header->offset;

    uint8_t* pointer = (uint8_t*) header + offset;

    if (offset + sizeof(DataHeader) + (sizeof(u_int8_t) * size) > mmf.size) {
        perror("Map buffer can not receive this large of data");
        return -1;
    }

    DataHeader* dataHeader = (DataHeader*) pointer;
    dataHeader->type = type;
    dataHeader->size = size;

    pointer = (uint8_t*) dataHeader + sizeof(DataHeader);
    memcpy(pointer, bytes, size);

    header->offset = offset + sizeof(DataHeader) + (size * sizeof(uint8_t));

    return 0;
}

int writeBytes(int isInput, uint8_t* bytes, size_t size) {
    return writeData(isInput, 1, bytes, size * sizeof(uint8_t));
}

int writeChars(int isInput, char* chars, size_t size) {
    return writeData(isInput, 2, (uint8_t *) chars, size * sizeof(char));
}

int writeInts(int isInput, int* ints, size_t size) {
    return writeData(isInput, 3, (uint8_t *) ints, size * sizeof(int));
}

int writeLongs(int isInput, int64_t* longs, size_t size) {
    return writeData(isInput, 4, (uint8_t *) longs, size * sizeof(int64_t));
}

int writeFloats(int isInput, float* floats, size_t size) {
    return writeData(isInput, 5, (uint8_t *) floats, size * sizeof(float));
}

int writeDoubles(int isInput, double* doubles, size_t size) {
    return writeData(isInput, 6, (uint8_t *) doubles, size * sizeof(double));
}

int flushWriteStream(int isInput) {

    Schema schema = getSchema();

    MappedFile mmf = isInput ? connectionFiles.input : connectionFiles.output;

    StreamHeader* header = (StreamHeader*) mmf.map;
    header->offset = sizeof(StreamHeader);

    uint8_t* pointer = (uint8_t*) header + sizeof(StreamHeader);

    if (isInput) {
        for (int i = 0; i < schema.header->inputByteSize; i++) {
            DataHeader* dataHeader = (DataHeader*) pointer;
            if (dataHeader->type != schema.inputBytes[i]) {
                perror("Data Types do not match");
                return -1;
            }

            size_t varCount = 1;
            for (int j = 0; j < schema.header->inputMatrixCols; j++) {
                varCount *= schema.inputMatrix[i * schema.header->inputMatrixCols + j];
            }

            if (dataHeader->size != varCount * getSizeofType(dataHeader->type)) {
                perror("Size does not match");
                return -1;
            }

            pointer += sizeof(DataHeader) + dataHeader->size;
        }
    } else {
        for (int i = 0; i < schema.header->outputByteSize; i++) {
            DataHeader* dataHeader = (DataHeader*) pointer;
            if (dataHeader->type != schema.outputBytes[i]) {
                return -1;
            }

            size_t varCount = 1;
            for (int j = 0; j < schema.header->outputMatrixCols; j++) {
                varCount *= schema.outputMatrix[i * schema.header->outputMatrixCols + j];
            }

            if (dataHeader->size != varCount * getSizeofType(dataHeader->type)) {
                perror("Size does not match");
                return -1;
            }

            pointer += sizeof(DataHeader) + dataHeader->size;
        }
    }

    return 0;
}

int readData(int isInput, uint8_t type, uint8_t* bytes, size_t size) {
    
    MappedFile mmf = isInput ? connectionFiles.input : connectionFiles.output;

    StreamHeader* header = (StreamHeader*) mmf.map;
    size_t offset = header->offset;

    uint8_t* pointer = (uint8_t*) header + offset;

    DataHeader* dataHeader = (DataHeader*) pointer;

    if (dataHeader->type != type || dataHeader->size != size) {
        perror("Type or size of data header doesn't match current parameters");
        return -1;
    }
    
    pointer += sizeof(DataHeader);

    for (int i = 0; i < size; i++) {
        bytes[i] = pointer[i]; 
    }

    header->offset = offset + sizeof(DataHeader) + dataHeader->size;

    return 0;
}

int readBytes(int isInput, uint8_t* bytes, size_t size) {
    return readData(isInput, 1, bytes, size * sizeof(uint8_t));
}

int readChars(int isInput, char* chars, size_t size) {
    return readData(isInput, 2, (uint8_t*) chars, size * sizeof(char));
}

int readInts(int isInput, int* ints, size_t size) {
    return readData(isInput, 3, (uint8_t*) ints, size * sizeof(int));
}

int readLongs(int isInput, int64_t* longs, size_t size) {
    return readData(isInput, 4, (uint8_t*) longs, size * sizeof(int64_t));
}

int readFloats(int isInput, float* floats, size_t size) {
    return readData(isInput, 5, (uint8_t*) floats, size * sizeof(float));
}

int readDoubles(int isInput, double* doubles, size_t size) {
    return readData(isInput, 6, (uint8_t*) doubles, size * sizeof(double));
}

int flushReadStream(int isInput) {
    
    MappedFile mmf = isInput ? connectionFiles.input : connectionFiles.output;

    StreamHeader* header = (StreamHeader*) mmf.map;
    header->offset = sizeof(StreamHeader);

    return 0;
}