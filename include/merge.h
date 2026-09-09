#ifndef MERGE_H
#define MERGE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define MERGE_MAGIC "MRGELF1"
#define MERGE_VERSION 1U

struct merge_footer {
    unsigned char magic[8];
    uint32_t version;
    uint32_t footer_size;
    uint64_t first_offset;
    uint64_t first_size;
    uint64_t second_offset;
    uint64_t second_size;
    char first_name[256];
    char second_name[256];
};

const char *merge_program_name(const char *path);
int merge_write_all(int fd, const void *buffer, size_t length);
int merge_pread_all(int fd, void *buffer, size_t length, off_t offset);
int merge_copy_range(int input, int output, uint64_t offset, uint64_t size);

int merge_read_footer(int fd, struct merge_footer *footer,
                      uint64_t *file_size);
int merge_validate_elf64(const char *path, uint64_t *size);

int merge_create_bundle(const char *first_path, const char *second_path,
                        const char *output_path);
int merge_run_bundle(int self_fd, const struct merge_footer *footer,
                     int argc, char **argv);

void merge_print_usage(const char *argv0);

#endif
