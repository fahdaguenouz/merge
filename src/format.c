#include "merge.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int merge_read_footer(int fd, struct merge_footer *footer, uint64_t *file_size)
{
    struct stat status;

    if (fstat(fd, &status) < 0 || status.st_size < 0)
        return -1;
    *file_size = (uint64_t)status.st_size;
    if (*file_size < sizeof(*footer))
        return 0;
    if (merge_pread_all(fd, footer, sizeof(*footer),
                        status.st_size - (off_t)sizeof(*footer)) < 0)
        return -1;
    if (memcmp(footer->magic, MERGE_MAGIC, sizeof(footer->magic)) != 0)
        return 0;
    if (footer->version != MERGE_VERSION ||
        footer->footer_size != sizeof(*footer)) {
        errno = EINVAL;
        return -1;
    }
    if (footer->first_offset > UINT64_MAX - footer->first_size ||
        footer->first_offset + footer->first_size != footer->second_offset ||
        footer->second_offset > UINT64_MAX - footer->second_size ||
        footer->second_offset + footer->second_size !=
            *file_size - sizeof(*footer)) {
        errno = EINVAL;
        return -1;
    }
    return 1;
}

int merge_validate_elf64(const char *path, uint64_t *size)
{
    Elf64_Ehdr header;
    struct stat status;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        fprintf(stderr, "merge: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode)) {
        fprintf(stderr, "merge: '%s' is not a regular file\n", path);
        close(fd);
        return -1;
    }
    if (status.st_size < (off_t)sizeof(header) ||
        merge_pread_all(fd, &header, sizeof(header), 0) < 0 ||
        memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 ||
        header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_VERSION] != EV_CURRENT ||
        header.e_version != EV_CURRENT ||
        header.e_machine != EM_X86_64 ||
        (header.e_type != ET_EXEC && header.e_type != ET_DYN)) {
        fprintf(stderr,
                "merge: '%s' is not a supported x86-64 little-endian ELF executable\n",
                path);
        close(fd);
        return -1;
    }
    *size = (uint64_t)status.st_size;
    return fd;
}
