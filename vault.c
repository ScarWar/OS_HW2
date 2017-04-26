#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>


#define START_DELIMITER "<<<<<<<<"
#define END_DELIMITER ">>>>>>>>"
#define ZERO_DELIMITER "000000000"
#define _FILE_OFFSET_BITS 64
#define max(s, t) ((s) > (t)) ? (s) : (t)
#define min(s, t) ((s) < (t)) ? (s) : (t)
#define K (1024)
#define M (1024 * 1024)
#define G (1024 * 1024 * 1024)
#define VAULT_SIZE sizeof(Vault)
#define DELIMITERS_SIZE 16
#define MAX_MAME_SIZE 257
#define MAX_NUM_OF_FILES 100
#define MAX_NUM_OF_GAPS 300
#define BUFFER_SIZE 4 * K
#define REPORT_ERR printf("Error - %s, Line %d\n",strerror(errno), __LINE__)
#define REPORT_ERR_MSG(msg) printf("Error - %s, Line %d\n",(msg), __LINE__)

typedef struct data_block_t {
    off_t dataBlockOffset;
    ssize_t dataBlockSize;
} DataBlock;

typedef struct file_allocation_table_t {
    char fileName[MAX_MAME_SIZE];
    ssize_t fileSize;
    mode_t fileProtaction;
    time_t insertionTime;
    short numberOfPartitons;
    DataBlock dataBlock[3];
} FAT;

typedef struct file_meta_data_t {
    ssize_t vaultSize;
    time_t creationTime;
    time_t lastModificationTime;
    short numberOfFiles;
} FileMetaData;

typedef struct gap_t {
    DataBlock gapBlocks[MAX_NUM_OF_GAPS];
    short numberOfGaps;
    ssize_t totalFreeMemory;
} Gaps;

typedef struct vault_t {
    FileMetaData fileMetaData;
    Gaps gaps;
    FAT files[MAX_NUM_OF_FILES];
} Vault;

typedef struct tiple_t {
    int index;
    off_t offset;
} Tuple;

Vault *readVault(int vfd) {
    ssize_t readNumber;
    if (vfd < 0) {
        REPORT_ERR_MSG("Invalid file descriptor");
        return 0;
    }
    Vault *vault;
    vault = malloc(VAULT_SIZE);
    if (vault == NULL) {
        REPORT_ERR;
        return NULL;
    }


    readNumber = read(vfd, vault, VAULT_SIZE);
    if (readNumber < 0) {
        REPORT_ERR;
        return NULL;
    }
    return vault;
}

int tupleCompare(const void *a, const void *b) {
    const Tuple at = *(const Tuple *) a;
    const Tuple bt = *(const Tuple *) b;
    return (int) (at.offset - bt.offset);
}

int getFileName(char *fullName, char *trgt) {
    if (fullName == NULL || trgt == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }
    int i;
    size_t j, size = strlen(fullName);
    for (i = (int) size - 1; i >= 0; --i) {
        if (fullName[i] == '/') {
            size = size - i;
            i++;
            for (j = 0; j < size; ++j) {
                trgt[j] = fullName[i++];
            }
            break;
        } else if (fullName[i] != '/' && i == 0) {
            strcpy(trgt, fullName);
            break;
        }
    }
    return 1;
}

int findFileInVault(Vault *vault, char *fileName) {
    int i;
    if (vault == NULL || fileName == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }
    for (i = 0; i < vault->fileMetaData.numberOfFiles; ++i) {
        if (strcmp(fileName, vault->files[i].fileName) == 0) {
            return i;
        }
    }
    return -1;
}

int saveVaultToFile(Vault *vault, int vfd) {
    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    } else if (vfd < 0) {
        REPORT_ERR_MSG("Invalid file descriptor");
        return 0;
    }
    off_t offset;
    ssize_t written;

    offset = lseek(vfd, 0, SEEK_SET);
    if (offset != 0) {
        REPORT_ERR_MSG("lseek function failed");
        return 0;
    }

    written = write(vfd, vault, VAULT_SIZE);
    if (written != VAULT_SIZE) {
        REPORT_ERR_MSG("Failed to save vault to file");
        return 0;
    }
    return 1;
}

int fileNameMaxWidth(Vault *vault) {
    size_t maxWidth = 0;
    int i;
    for (i = 0; i < vault->fileMetaData.numberOfFiles; ++i)
        maxWidth = max(strlen(vault->files[i].fileName), maxWidth);
    return (int) maxWidth;
}

ssize_t formatSize(ssize_t fileSize, char *sizeType) {
    ssize_t approxSize;
    if (sizeType == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
    }
    if (fileSize <= K) {
        *sizeType = 'B';
        approxSize = fileSize;
    } else if (fileSize <= M) {
        *sizeType = 'K';
        approxSize = fileSize / K;
    } else if (fileSize <= G) {
        *sizeType = 'M';
        approxSize = fileSize / M;
    } else {
        *sizeType = 'G';
        approxSize = fileSize / G;
    }
    return approxSize;
}

int sortIndexByOffset(Gaps *gap, int *blockIndexes, int numberOfIndexes) {
    int k;
    if (gap == NULL || blockIndexes == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }
    if (numberOfIndexes < 0) {
        REPORT_ERR_MSG("Invalid argument, index must be non-negative");
        return 0;
    } else if (numberOfIndexes > 3) {
        REPORT_ERR_MSG("Invalid argument, index must be smaller then 3");
        return 0;
    }

    Tuple tupleArray[3];
    for (k = 0; k < numberOfIndexes; ++k) {
        tupleArray[k].offset = gap->gapBlocks[blockIndexes[k]].dataBlockOffset;
        tupleArray[k].index = blockIndexes[k];
    }
    qsort(tupleArray, (size_t) numberOfIndexes, sizeof(Tuple), tupleCompare);
    for (k = 0; k < numberOfIndexes; ++k) {
        blockIndexes[k] = tupleArray[k].index;
    }
    return 1;
}

int findGap(const Gaps *gaps, ssize_t *fileSize, int blockIndexes[], int numberOfIndexes) {
    char continueFlag;
    int currBlock = -1, j, i;
    ssize_t maxBlockSize = 0, blockSize;

    if (gaps == NULL || fileSize == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }

    for (i = 0; i < gaps->numberOfGaps; ++i) {
        continueFlag = 0;
        // Check if the gas was already selected
        for (j = 0; j < numberOfIndexes; j++) {
            if (blockIndexes[j] == i) {
                continueFlag = 1;
                break;
            }
        }
        if (continueFlag == 1) {
            continue;
        }

        blockSize = gaps->gapBlocks[i].dataBlockSize;
        // Find the smallest gap which fits or find largest
        if (maxBlockSize < blockSize && maxBlockSize <= (*fileSize)) {
            // maxBlockSize is smaller than fileSize and new bigger gap was found
            maxBlockSize = blockSize;
            currBlock = i;
        } else if (maxBlockSize > blockSize && blockSize >= (*fileSize)) {
            maxBlockSize = blockSize;
            currBlock = i;
        }
    }
    (*fileSize) -= maxBlockSize;
    return currBlock;
}

short findGaps(int blockIndexes[], ssize_t blockSizes[], Gaps *gaps, ssize_t fileSize) {
    int index;
    ssize_t tmpFileSize;
    if (gaps == NULL || fileSize < 0) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }

    tmpFileSize = fileSize + DELIMITERS_SIZE;
    if (gaps->totalFreeMemory < tmpFileSize) {
        REPORT_ERR_MSG("No enough free memory, try to delete files");
        return 0;
    }

    // Find first gap
    index = findGap(gaps, &tmpFileSize, blockIndexes, 0);
    if (index == -1) return -1;
    blockIndexes[0] = index;
    blockSizes[0] = fileSize + DELIMITERS_SIZE;
    if (tmpFileSize <= 0)
        return 1;

    // Find second gap
    tmpFileSize += DELIMITERS_SIZE;
    index = findGap(gaps, &tmpFileSize, blockIndexes, 1);
    if (index == -1) return -1;
    blockIndexes[1] = index;
    if (!sortIndexByOffset(gaps, blockIndexes, 2)) return -1;
    blockSizes[0] = gaps->gapBlocks[blockIndexes[0]].dataBlockSize;
    blockSizes[1] = fileSize - blockSizes[0] + 2 * DELIMITERS_SIZE;
    if (tmpFileSize <= 0)
        return 2;

    // Find third gap
    tmpFileSize += DELIMITERS_SIZE;
    index = findGap(gaps, &tmpFileSize, blockIndexes, 2);
    if (index == -1) return -1;
    blockIndexes[2] = index;
    if (!sortIndexByOffset(gaps, blockIndexes, 3)) return -1;
    blockSizes[0] = gaps->gapBlocks[blockIndexes[0]].dataBlockSize;
    blockSizes[1] = gaps->gapBlocks[blockIndexes[1]].dataBlockSize;
    blockSizes[2] = fileSize -
                    blockSizes[0] -
                    blockSizes[1] +
                    3 * DELIMITERS_SIZE;
    if (tmpFileSize <= 0)
        return 3;
    REPORT_ERR_MSG("No enough free memory. Use defragmentation and try to add again");
    return 0;
}

int mergeGaps(Vault *vault, int i, int j) {
    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }
    DataBlock tmpGap;

    // Add block size to previous gap
    vault->gaps.gapBlocks[i].dataBlockSize += vault->gaps.gapBlocks[j].dataBlockSize;

    // Save last gap to swap with
    tmpGap = vault->gaps.gapBlocks[vault->gaps.numberOfGaps - 1];

    // Reset gap
    vault->gaps.gapBlocks[j].dataBlockSize = 0;
    vault->gaps.gapBlocks[j].dataBlockOffset = 0;

    // Put empty gap at the end
    vault->gaps.gapBlocks[vault->gaps.numberOfGaps - 1] = vault->gaps.gapBlocks[j];
    vault->gaps.gapBlocks[j] = tmpGap;
    vault->gaps.numberOfGaps--;
    return 1;
}

int mergeGap(Vault *vault, DataBlock *gap, short index) {
    int i, prev = -1, next = -1;
    DataBlock __gap;

    if (gap == NULL || vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }

    for (i = 0; i < vault->gaps.numberOfGaps; i++) {
        __gap = vault->gaps.gapBlocks[i];

        // If gap is successor to __gap then save __gap index for future merge
        if (__gap.dataBlockOffset +
            __gap.dataBlockSize ==
            gap->dataBlockOffset)
            prev = i;

        // If __gap is successor to gap then save __gap index for future merge
        if (gap->dataBlockOffset +
            gap->dataBlockSize ==
            __gap.dataBlockOffset)
            next = i;
    }

    if (prev != -1 && next != -1) {
        if (!mergeGaps(vault, prev, index)) { return 0; }
        if (!mergeGaps(vault, prev, next)) { return 0; }
        return 2;
    } else if (prev != -1) {
        if (!mergeGaps(vault, prev, index)) { return 0; }
        return 1;
    } else if (next != -1) {
        if (!mergeGaps(vault, index, next)) { return 0; }
        return 1;
    }
    return 0;
}

int removeGap(Vault *vault, int gapIndex) {
    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    } else if (gapIndex < 0) {
        REPORT_ERR_MSG("Invalid argument, index must be non-negative");
        return 0;
    }
    if (vault->gaps.numberOfGaps == 1)
        return 1;
    vault->gaps.gapBlocks[gapIndex] = vault->gaps.gapBlocks[vault->gaps.numberOfGaps - 1];
    vault->gaps.numberOfGaps--;
    return 1;
}

int writeToBlock(int vfd, int ifd, ssize_t dataToWrite, DataBlock dataBlock, off_t inputOffset) {
    ssize_t written, readSize, readBufferSize;
    off_t offset;
    char *buffer;
    buffer = malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        REPORT_ERR;
        return 0;
    }

    offset = lseek(vfd, dataBlock.dataBlockOffset, SEEK_SET);
    if (offset != dataBlock.dataBlockOffset) {
        REPORT_ERR;
        return 0;
    }

    lseek(ifd, inputOffset, SEEK_SET);

    written = write(vfd, START_DELIMITER, 8);
    if (written != 8) {
        REPORT_ERR;
        return 0;
    }

    while (dataToWrite > 0) {

        // Read from input file
        readBufferSize = min(dataToWrite, BUFFER_SIZE);
        readSize = read(ifd, buffer, (size_t) readBufferSize);
        if (readSize < 0) {
            REPORT_ERR;
            if (write(vfd, END_DELIMITER, 8) != 8)
                REPORT_ERR_MSG("Failed to exit safely, data may have been corrupted");
            return 0;
        }


        // Write to vault
        written = write(vfd, buffer, (size_t) readBufferSize);
        if (written != readBufferSize) {
            REPORT_ERR;
            if (write(vfd, END_DELIMITER, 8) != 8)
                REPORT_ERR_MSG("Failed to exit safely, data may have been corrupted");
            return 0;
        }

        // Update how much left to write
        dataToWrite -= written;
    }

    written = write(vfd, END_DELIMITER, 8);
    if (written != 8) {
        REPORT_ERR;
        return 0;
    }
    return 1;
}

int findLastGap(Gaps *gap) {
    int i, maxIndex = -1;
    off_t offset = 0;

    if (gap == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }

    for (i = 0; i < gap->numberOfGaps; ++i) {
        if (offset < gap->gapBlocks[i].dataBlockOffset) {
            offset = gap->gapBlocks[i].dataBlockOffset;
            maxIndex = i;
        }
    }
    return maxIndex;
}

int findFirstGap(Gaps *gap) {
    int i, minIndex = -1;
    off_t offset = 0;

    if (gap == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }

    if (gap->numberOfGaps == 1) {
        return 0;
    }

    offset = gap->gapBlocks[0].dataBlockOffset;

    for (i = 0; i < gap->numberOfGaps; ++i) {
        if (offset > gap->gapBlocks[i].dataBlockOffset) {
            offset = gap->gapBlocks[i].dataBlockOffset;
            minIndex = i;
        }
    }
    return minIndex;
}

short findNearGapIndex(Gaps gap, off_t offset) {
    short i;
    for (i = 0; i < gap.numberOfGaps; ++i) {
        if (gap.gapBlocks[i].dataBlockOffset +
            gap.gapBlocks[i].dataBlockSize ==
            offset) {
            return i;
        }
    }
    return -1;
}

double getFragmentationRation(Vault *vault) {
    int firstIndex, lastIndex;
    short removeFirst = 0, removeLast = 0;
    ssize_t consumedMemory = 0, fragmentedMemory = vault->gaps.totalFreeMemory;

    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }

    firstIndex = findFirstGap(&vault->gaps);
    lastIndex = findLastGap(&vault->gaps);

    if (firstIndex == -1 || lastIndex == -1) {
        return -1;
    }

    if (vault->gaps.gapBlocks[firstIndex].dataBlockOffset == VAULT_SIZE) {
        removeFirst = 1;
    }

    if (vault->gaps.gapBlocks[lastIndex].dataBlockOffset +
        vault->gaps.gapBlocks[lastIndex].dataBlockSize ==
        vault->fileMetaData.vaultSize) {
        removeLast = 1;
    }

    if (removeFirst && removeLast) {
        consumedMemory =
                vault->gaps.gapBlocks[lastIndex].dataBlockOffset -
                vault->gaps.gapBlocks[firstIndex].dataBlockSize +
                vault->gaps.gapBlocks[firstIndex].dataBlockOffset;
        fragmentedMemory -=
                vault->gaps.gapBlocks[lastIndex].dataBlockSize +
                vault->gaps.gapBlocks[firstIndex].dataBlockSize;

    } else if (removeFirst) {
        consumedMemory =
                vault->fileMetaData.vaultSize -
                vault->gaps.gapBlocks[firstIndex].dataBlockSize +
                vault->gaps.gapBlocks[firstIndex].dataBlockOffset;
        fragmentedMemory -=
                vault->gaps.gapBlocks[firstIndex].dataBlockSize;
    } else if (removeLast) {
        consumedMemory =
                vault->gaps.gapBlocks[lastIndex].dataBlockOffset -
                VAULT_SIZE;
        fragmentedMemory -=
                vault->gaps.gapBlocks[lastIndex].dataBlockSize;
    } else {
        consumedMemory =
                vault->fileMetaData.vaultSize -
                VAULT_SIZE;
    }
    return (double) fragmentedMemory / (double) (consumedMemory);

}

size_t getTotalNumberOfPartitions(Vault *vault) {
    size_t numOfPartitions = 0;
    int i;

    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }
    for (i = 0; i < vault->fileMetaData.numberOfFiles; ++i) {
        numOfPartitions += vault->files[i].numberOfPartitons;
    }
    return numOfPartitions;
}

char toLowercase(char i) {
    if ('A' <= i && i <= 'Z')
        return i + (char) ('a' - 'A');
    return i;
}

long getSizeFromString(char *string) {
    long size;
    char type;
    if (string == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return -1;
    }
    // Read data from string
    if (sscanf(string, "%ld%c", &size, &type) != 2) {
        REPORT_ERR_MSG("Invalid data formant was entered");
        return -1;
    }

    type = toLowercase(type);

    // Calculate data size
    if (type == 'b')
        return size;
    else if (type == 'k')
        return size * K;
    else if (type == 'm')
        return size * M;
    else if (type == 'G')
        return size * G;
    REPORT_ERR_MSG("Unknown data type was entered");
    return -1;
}

ssize_t init(int vfd, long dataSize) {
    ssize_t written;
    off_t offset;
    Vault *vault;

    if (dataSize < VAULT_SIZE) {
        REPORT_ERR_MSG("Entered vault size isn't large enough to create a vault");
        return 0;
    }

    vault = malloc(VAULT_SIZE);
    if (vault == NULL) {
        REPORT_ERR;
        return 0;
    }

    // Add File meta data
    vault->fileMetaData.vaultSize = dataSize;
    vault->fileMetaData.creationTime = time(0);
    vault->fileMetaData.lastModificationTime = vault->fileMetaData.creationTime;
    vault->fileMetaData.numberOfFiles = 0;

    // Add gap data
    vault->gaps.totalFreeMemory = dataSize - VAULT_SIZE;
    vault->gaps.numberOfGaps = 1;
    vault->gaps.gapBlocks[0].dataBlockSize = dataSize - VAULT_SIZE;
    vault->gaps.gapBlocks[0].dataBlockOffset = VAULT_SIZE;

    // Create file of size dataSize using lseek
    written = write(vfd, "#", 1);
    if (written < 0) {
        REPORT_ERR;
        return 0;
    }

    offset = lseek(vfd, dataSize - 1, SEEK_SET);
    if (offset == -1 || offset != dataSize - 1) {
        REPORT_ERR;
        return 0;
    }

    written = write(vfd, "#", 1);
    if (written < 0) {
        REPORT_ERR;
        return 0;
    }

    offset = lseek(vfd, 0, SEEK_SET);
    if (offset != 0) {
        REPORT_ERR;
        return 0;
    }

    written = write(vfd, vault, VAULT_SIZE);
    if (written != VAULT_SIZE) {
        REPORT_ERR;
        return 0;
    }

    free(vault);
    printf("Result: A vault created\n");
    return 1;
}

int addFile(Vault *vault, int vfd, char *fileName) {
    int ifd, i, newFileIndex;
    short numberOfGaps;
    int blockIndex[3] = {-1, -1, -1};
    ssize_t blockSizes[3] = {0, 0, 0};
    ssize_t totalGapsSize = 0;
    DataBlock gap;
    off_t inputOffset = 0;
    char newName[247] = "";

    if (vault == NULL || fileName == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }

    // Max number of files is 100
    if (vault->fileMetaData.numberOfFiles == 100) {
        REPORT_ERR_MSG("Can't add more files to vault, delete some files and try again");
        return 0;
    }

    // Set newName to be filName without path
    if (!getFileName(fileName, newName)) {
        return 0;
    }
    // If file name already exists report error
    if (findFileInVault(vault, newName) != -1) {
        REPORT_ERR_MSG("Unable to add file, file with same name already exists, rename file and add again");
        return 0;
    }

    struct stat sb;
    stat(fileName, &sb);

    ifd = open(fileName, O_RDONLY, S_IRUSR | S_IWUSR);
    if (ifd < 0) {
        REPORT_ERR;
        return 0;
    }


    numberOfGaps = findGaps(blockIndex, blockSizes, &vault->gaps, sb.st_size);
    if (numberOfGaps == -1) // Error in finding a gap
        return 0;
    else if (numberOfGaps == 0) {
        REPORT_ERR_MSG("Unable to add file, no valid partition exists");
        return 0;
    }
    newFileIndex = vault->fileMetaData.numberOfFiles;
    for (i = 0; i < numberOfGaps; i++) {
        gap = vault->gaps.gapBlocks[blockIndex[i]];

        if (!writeToBlock(vfd, ifd, blockSizes[i] - DELIMITERS_SIZE, gap, inputOffset)) {
            // TODO error message
            return -1;
        }
        inputOffset += blockSizes[i] - DELIMITERS_SIZE;

        // Adding file to vault
        vault->files[newFileIndex].dataBlock[i].dataBlockSize = blockSizes[i];
        vault->files[newFileIndex].dataBlock[i].dataBlockOffset = gap.dataBlockOffset;
        vault->files[newFileIndex].numberOfPartitons++;
        totalGapsSize += gap.dataBlockSize;

        // If last gap isn't fully used create new gap with what left
        if (blockSizes[i] < gap.dataBlockSize && i == numberOfGaps - 1) {
            vault->gaps.gapBlocks[blockIndex[i]].dataBlockSize = gap.dataBlockSize - blockSizes[i];
            vault->gaps.gapBlocks[blockIndex[i]].dataBlockOffset += blockSizes[i];
            break;
        }

        if (!removeGap(vault, blockIndex[i])) {
            return -1;
        }
    }

    // Fill new file with input file information and update stats
    strcpy(vault->files[newFileIndex].fileName, newName);
    vault->files[newFileIndex].fileProtaction = sb.st_mode;
    vault->files[newFileIndex].fileSize = sb.st_size + numberOfGaps * DELIMITERS_SIZE;
    vault->files[newFileIndex].insertionTime = time(0);
    vault->files[newFileIndex].numberOfPartitons = numberOfGaps;
    vault->fileMetaData.numberOfFiles++;

    // Update vault stats
    vault->gaps.totalFreeMemory -= vault->files[newFileIndex].fileSize;
    vault->fileMetaData.lastModificationTime = time(0);
    close(ifd);

    printf("Result: %s inserted\n", newName);
    return 1;
}

int removeFile(Vault *vault, int vfd, int fileIndex) {
    short numGap;
    int i;
    off_t offset;
    ssize_t written;
    FAT file;
    file = vault->files[fileIndex];
    for (i = 0; i < file.numberOfPartitons; ++i) {

        offset = lseek(vfd, file.dataBlock[i].dataBlockOffset, SEEK_SET);
        if (offset != file.dataBlock[i].dataBlockOffset) {
            // TODO error message
            return 0;
        }

        written = write(vfd, ZERO_DELIMITER, 8);
        if (written != 8) {
            // TODO error message
            return 0;
        }

        lseek(vfd, file.dataBlock[i].dataBlockSize - 16, SEEK_CUR);

        written = write(vfd, ZERO_DELIMITER, 8);
        if (written != 8) {
            // TODO error message
            return 0;
        }

        // Add removed file as a gap
        numGap = vault->gaps.numberOfGaps;
        vault->gaps.totalFreeMemory += file.dataBlock[i].dataBlockSize;
        vault->gaps.gapBlocks[numGap].dataBlockOffset = file.dataBlock[i].dataBlockOffset;
        vault->gaps.gapBlocks[numGap].dataBlockSize = file.dataBlock[i].dataBlockSize;
        vault->gaps.numberOfGaps++;

        // Merge newly created gap if possible
        if (mergeGap(vault, &vault->gaps.gapBlocks[numGap], numGap) == -1) {
            // TODO error message
            return 0;
        }
    }

    // Remove from vault files by overriding with last file and update status
    vault->files[fileIndex] = vault->files[vault->fileMetaData.numberOfFiles - 1];
    vault->fileMetaData.numberOfFiles--;
    vault->fileMetaData.lastModificationTime = time(0);

    printf("Result: %s deleted\n", file.fileName);
    return 1;
}

int fetchFile(Vault *vault, int vfd, int fileIndex) {
    char *buf;
    ssize_t readBufferSize;
    ssize_t writeSize, written, readNumber;
    FAT file;
    int i, ofd;

    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }
    if (fileIndex < 0) {
        REPORT_ERR_MSG("Invalid file descriptor");
        return 0;
    }
    buf = malloc(BUFFER_SIZE);
    if (buf == NULL) {
        REPORT_ERR;
        return 0;
    }

    // TODO add permissions check

    file = vault->files[fileIndex];
    ofd = open(file.fileName, O_WRONLY | O_CREAT | O_TRUNC, file.fileProtaction);
    if (ofd < 0) {
        REPORT_ERR;
        return 0;
    }

    for (i = 0; i < file.numberOfPartitons; ++i) {
        lseek(vfd, file.dataBlock[i].dataBlockOffset + 8, SEEK_SET);
        writeSize = file.dataBlock[i].dataBlockSize - 16;
        while (writeSize > 0) {
            readBufferSize = min(writeSize, BUFFER_SIZE);
            readNumber = read(vfd, buf, (size_t) readBufferSize);
            if (readNumber < 0) {
                REPORT_ERR;
                return 0;
            }
            written = write(ofd, buf, (size_t) readNumber);
            if (written != readBufferSize) {
                REPORT_ERR;
                return 0;
            }
            writeSize -= written;
        }
    }
    free(buf);
    close(ofd);
    printf("Result: %s created\n", vault->files[fileIndex].fileName);
    return 1;
}

int status(Vault *vault) {
    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }

    double fragRatio = getFragmentationRation(vault);
    if (fragRatio < 0) {
        return 0;
    }
    printf("%-21s %d\n", "Number of files:", vault->fileMetaData.numberOfFiles);
    printf("%-21s %zuB\n", "Total size:", vault->fileMetaData.vaultSize -
                                          vault->gaps.totalFreeMemory -
                                          VAULT_SIZE);
    printf("%-21s %lg\n", "Fragmentation ratio:", fragRatio);
    return 1;
}

int list(Vault *vault) {

    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    }

    int width = fileNameMaxWidth(vault), i;
    // Print list of files in a table format
    for (i = 0; i < vault->fileMetaData.numberOfFiles; ++i) {
        char sizeType;
        ssize_t size = formatSize(vault->files[i].fileSize, &sizeType);
        printf("%-*s %5zu%c 0%o %s", width + 1, vault->files[i].fileName, size, sizeType,
               0777 & vault->files[i].fileProtaction,
               ctime(&vault->files[i].insertionTime));
    }
    // Update file modification time
    vault->fileMetaData.lastModificationTime = time(0);
    return 1;
}

int defragmentation(Vault *vault, int vfd) {
    FAT file;
    ssize_t written, readNumber, dataToWrite;
    size_t bufSize;
    off_t readOffset, writeOffset;
    int i, j, k = 0;
    short gapIndex;
    size_t numOfPartitions;
    char *buf;

    if (vault == NULL) {
        REPORT_ERR_MSG("Null pointer argument");
        return 0;
    } else if (vfd < 0) {
        REPORT_ERR_MSG("Invalid file descriptor");
        return 0;
    }

    // Create and fill array of all offsets, then sort them according to offset
    numOfPartitions = getTotalNumberOfPartitions(vault);
    Tuple *tupleArray = malloc(numOfPartitions * sizeof(Tuple));
    if (tupleArray == NULL) {
        REPORT_ERR;
        return 0;
    }
    for (i = 0; i < vault->fileMetaData.numberOfFiles; ++i) {
        for (j = 0; j < vault->files[i].numberOfPartitons; ++j) {
            tupleArray[k].index = i;
            tupleArray[k++].offset = vault->files[i].dataBlock[j].dataBlockOffset;
        }
    }
    qsort(tupleArray, numOfPartitions, sizeof(Tuple), tupleCompare);

    buf = malloc(BUFFER_SIZE);
    if (buf == NULL) {
        REPORT_ERR;
        return 0;
    }

    for (i = 0; i < numOfPartitions; ++i) {
        file = vault->files[tupleArray[i].index];
        gapIndex = findNearGapIndex(vault->gaps, tupleArray[i].offset);
        if (gapIndex != -1) {
            // Go to the start of the DataBlock
            lseek(vfd, tupleArray[i].offset, SEEK_SET);
            // Remove start delimiter
            written = write(vfd, ZERO_DELIMITER, 8);
            if (written != 8) {
                REPORT_ERR;
                return 0;
            }
            // Find partition index with offset
            for (j = 0; j < file.numberOfPartitons; ++j)
                if (tupleArray[i].offset == file.dataBlock[j].dataBlockOffset)
                    break;

            lseek(vfd, file.dataBlock[j].dataBlockSize - 16, SEEK_CUR);
            // Remove end delimiter
            written = write(vfd, ZERO_DELIMITER, 8);
            if (written != 8) {
                REPORT_ERR;
                return 0;
            }

            // Move to the start of the gap
            writeOffset = lseek(vfd, vault->gaps.gapBlocks[gapIndex].dataBlockOffset, SEEK_SET);

            // Move DataBlock to new location
            written = write(vfd, START_DELIMITER, 8);
            if (written != 8) {
                REPORT_ERR;
                return 0;
            }
            writeOffset += 8;
            readOffset = file.dataBlock[j].dataBlockOffset + 8;

            dataToWrite = file.dataBlock[j].dataBlockSize - 16;
            // Move data
            while (dataToWrite > 0) {
                bufSize = (size_t) (min(dataToWrite, BUFFER_SIZE));

                // Jump to reading position and read
                lseek(vfd, readOffset, SEEK_SET);
                readNumber = read(vfd, buf, bufSize);
                if (readNumber < 0) {
                    REPORT_ERR;
                    return 0;
                }
                readOffset += readNumber;

                // Jump to writing position and write
                lseek(vfd, writeOffset, SEEK_SET);
                written = write(vfd, buf, bufSize);
                if (written != bufSize) {
                    REPORT_ERR;
                    if (write(vfd, END_DELIMITER, 8) != 8)
                        REPORT_ERR;
                    return 0;
                }
                writeOffset += written;

                dataToWrite -= written;
            }

            written = write(vfd, END_DELIMITER, 8);
            if (written != 8) {
                REPORT_ERR;
                return 0;
            }
            // Update file position
            vault->files[tupleArray[i].index].dataBlock[j].dataBlockOffset =
                    vault->gaps.gapBlocks[gapIndex].dataBlockOffset;

            // Update gap position and size
            vault->gaps.gapBlocks[gapIndex].dataBlockOffset = writeOffset + 8;

            // Merge gaps if needed
            if (mergeGap(vault, &vault->gaps.gapBlocks[gapIndex], gapIndex) == -1) {
                return 0;
            }

        }
    }
    free(tupleArray);
    free(buf);
    printf("Result: Defragmentation complete\n");
    return 1;

}

int main(int argc, char **argv) {
    struct timeval start, end;
    long seconds, useconds;
    double mtime;
    int vfd, index;
    int errorFlag = 0;
    long vaultSize = 0;
    char *vaultName, *opName, *p;

    // start time measurement
    gettimeofday(&start, NULL);


    if (argc > 4 || 3 > argc) {
        REPORT_ERR_MSG("Invalid number of arguments");
        errorFlag = -1;
    }

    vaultName = argv[1];
    opName = argv[2];

    // Make opName to be lowercase only
    // Credit to J.F. Sebastian - Stackoverflow.com
    // http://stackoverflow.com/questions/2661766/c-convert-a-mixed-case-string-to-all-lower-case#comment2679128_2661788
    p = opName;
    for (; *p; ++p) *p = toLowercase(*p);

    if (errorFlag != -1) {
        if (strcmp(opName, "init") == 0) {
            vfd = open(vaultName, O_WRONLY | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
            if (vfd < 0) {
                printf("Error - Failed to create a vault file\n");
                errorFlag = -1;
            } else {
            }
            vaultSize = getSizeFromString(argv[3]);
            if (vaultSize == -1)
                errorFlag = -1;
            else if (!init(vfd, vaultSize))
                errorFlag = -1;
            close(vfd);
        } else if (strcmp(opName, "list") == 0) {
            vfd = open(vaultName, O_RDWR, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (vfd < 0) {
                printf("Error - Failed to open vault file\n");
                errorFlag = -1;
            } else {
                Vault *vault = readVault(vfd);
                if (vault != NULL) {
                    if (!list(vault)) {
                        errorFlag = -1;
                    }
                } else
                    errorFlag = -1;
            }
            close(vfd);
        } else if (strcmp(opName, "add") == 0) {
            vfd = open(vaultName, O_RDWR, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (vfd < 0) {
                printf("Error - Failed to open vault file\n");
                errorFlag = -1;
            } else {
                Vault *vault = readVault(vfd);
                if (vault != NULL) {
                    errorFlag = addFile(vault, vfd, argv[3]);
                    if (errorFlag < 0)
                        errorFlag = -1;
                    else if (!errorFlag)
                        errorFlag = -1;
                    else if (!saveVaultToFile(vault, vfd))
                        errorFlag = -1;
                } else
                    errorFlag = -1;
            }
            close(vfd);
        } else if (strcmp(opName, "rm") == 0) {
            vfd = open(vaultName, O_RDWR, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (vfd < 0) {
                printf("Error - Failed to open vault file\n");
                errorFlag = -1;
            } else {
                Vault *vault = readVault(vfd);
                if (vault != NULL) {
                    index = findFileInVault(vault, argv[3]);
                    if (index == -1) {
                        printf("Error - File does not exist in the vault\n");
                        errorFlag = -1;
                    } else if (!removeFile(vault, vfd, index)) {
                        errorFlag = -1;
                    } else if (!saveVaultToFile(vault, vfd)) {
                        errorFlag = -1;
                    }
                } else
                    errorFlag = -1;
            }
            close(vfd);
        } else if (strcmp(opName, "fetch") == 0) {
            vfd = open(vaultName, O_RDWR, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (vfd < 0) {
                printf("Error - Failed to open vault file\n");
                errorFlag = -1;
            } else {
                Vault *vault = readVault(vfd);
                if (vault != NULL) {
                    index = findFileInVault(vault, argv[3]);
                    if (index == -1) {
                        printf("Error - File does not exist in the vault\n");
                        errorFlag = -1;
                    } else if (!fetchFile(vault, vfd, index)) {
                        errorFlag = -1;
                    } else if (!saveVaultToFile(vault, vfd)) {
                        errorFlag = -1;
                    }
                } else
                    errorFlag = -1;
            }
            close(vfd);
        } else if (strcmp(opName, "defrag") == 0) {
            vfd = open(vaultName, O_RDWR, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (vfd < 0) {
                printf("Error - Failed to open vault file\n");
                errorFlag = -1;
            } else {
                Vault *vault = readVault(vfd);
                if (vault != NULL) {
                    if (!defragmentation(vault, vfd)) {
                        errorFlag = -1;
                    } else if (!saveVaultToFile(vault, vfd)) {
                        errorFlag = -1;
                    }
                } else
                    errorFlag = -1;
            }
            close(vfd);
        } else if (strcmp(opName, "status") == 0) {
            vfd = open(vaultName, O_RDWR, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            if (vfd < 0) {
                printf("Error - Failed to open vault file\n");
                errorFlag = -1;
            } else {
                Vault *vault = readVault(vfd);
                if (vault != NULL) {
                    if (!status(vault)) {
                        errorFlag = -1;
                    }
                } else
                    errorFlag = -1;
            }
            close(vfd);
        } else {
            REPORT_ERR_MSG("Unknown operation");
        }
    }

    // end time measurement and print result
    gettimeofday(&end, NULL);

    seconds = end.tv_sec - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;

    mtime = ((seconds) * 1000 + useconds / 1000.0);
    printf("Elapsed time: %.3f milliseconds\n", mtime);
    return errorFlag;
}