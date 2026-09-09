#include "merge.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define COPY_BUFFER_SIZE (64U * 1024U)

const char *merge_program_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

int merge_write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = buffer;

    while (length > 0) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        cursor += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

int merge_pread_all(int fd, void *buffer, size_t length, off_t offset)
{
    unsigned char *cursor = buffer;

    while (length > 0) {
        ssize_t amount = pread(fd, cursor, length, offset);
        if (amount < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (amount == 0) {
            errno = EIO;
            return -1;
        }
        cursor += (size_t)amount;
        offset += amount;
        length -= (size_t)amount;
    }
    return 0;
}

int merge_copy_range(int input, int output, uint64_t offset, uint64_t size)
{
    unsigned char buffer[COPY_BUFFER_SIZE];

    while (size > 0) {
        size_t wanted = size < sizeof(buffer) ? (size_t)size : sizeof(buffer);
        if (offset > (uint64_t)INT64_MAX ||
            merge_pread_all(input, buffer, wanted, (off_t)offset) < 0 ||
            merge_write_all(output, buffer, wanted) < 0)
            return -1;
        offset += wanted;
        size -= wanted;
    }
    return 0;
}
