#include "merge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void set_payload_name(char destination[256], const char *path)
{
    snprintf(destination, 256, "%s", merge_program_name(path));
}

int merge_create_bundle(const char *first_path, const char *second_path,
                        const char *output_path)
{
    struct merge_footer footer = {0};
    struct stat self_status;
    uint64_t first_size;
    uint64_t second_size;
    char temporary[PATH_MAX] = {0};
    int self_fd = -1;
    int first_fd = -1;
    int second_fd = -1;
    int output_fd = -1;
    int result = EXIT_FAILURE;

    first_fd = merge_validate_elf64(first_path, &first_size);
    if (first_fd < 0)
        goto done;
    second_fd = merge_validate_elf64(second_path, &second_size);
    if (second_fd < 0)
        goto done;
    self_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (self_fd < 0 || fstat(self_fd, &self_status) < 0 || self_status.st_size < 0) {
        fprintf(stderr, "merge: cannot read the binder executable: %s\n",
                strerror(errno));
        goto done;
    }
    if ((uint64_t)self_status.st_size > UINT64_MAX - first_size ||
        (uint64_t)self_status.st_size + first_size > UINT64_MAX - second_size) {
        fprintf(stderr, "merge: combined output is too large\n");
        goto done;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", output_path,
                 (long)getpid()) >= (int)sizeof(temporary)) {
        fprintf(stderr, "merge: output path is too long\n");
        goto done;
    }
    output_fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    if (output_fd < 0) {
        fprintf(stderr, "merge: cannot create '%s': %s\n", temporary,
                strerror(errno));
        goto done;
    }

    memcpy(footer.magic, MERGE_MAGIC, sizeof(footer.magic));
    footer.version = MERGE_VERSION;
    footer.footer_size = sizeof(footer);
    footer.first_offset = (uint64_t)self_status.st_size;
    footer.first_size = first_size;
    footer.second_offset = footer.first_offset + first_size;
    footer.second_size = second_size;
    set_payload_name(footer.first_name, first_path);
    set_payload_name(footer.second_name, second_path);

    if (merge_copy_range(self_fd, output_fd, 0, footer.first_offset) < 0 ||
        merge_copy_range(first_fd, output_fd, 0, first_size) < 0 ||
        merge_copy_range(second_fd, output_fd, 0, second_size) < 0 ||
        merge_write_all(output_fd, &footer, sizeof(footer)) < 0 ||
        fsync(output_fd) < 0) {
        fprintf(stderr, "merge: failed while writing '%s': %s\n", temporary,
                strerror(errno));
        goto done;
    }
    if (close(output_fd) < 0) {
        output_fd = -1;
        fprintf(stderr, "merge: failed to close '%s': %s\n", temporary,
                strerror(errno));
        goto done;
    }
    output_fd = -1;
    if (rename(temporary, output_path) < 0) {
        fprintf(stderr, "merge: cannot install '%s': %s\n", output_path,
                strerror(errno));
        goto done;
    }
    printf("%s and %s merged into %s successfully!\n", first_path, second_path,
           output_path);
    result = EXIT_SUCCESS;

done:
    if (output_fd >= 0)
        close(output_fd);
    if (result != EXIT_SUCCESS && temporary[0] != '\0')
        unlink(temporary);
    if (self_fd >= 0)
        close(self_fd);
    if (first_fd >= 0)
        close(first_fd);
    if (second_fd >= 0)
        close(second_fd);
    return result;
}
