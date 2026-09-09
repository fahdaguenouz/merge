#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MERGE_MAGIC "MRGELF1"
#define MERGE_VERSION 1U
#define COPY_BUFFER_SIZE (64U * 1024U)

/*
 * This fixed-size trailer is appended after the two ELF payloads. All integer
 * values use the native little-endian representation required by this tool's
 * x86-64 Linux target.
 */
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

static const char *program_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static int write_all(int fd, const void *buffer, size_t length)
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

static int pread_all(int fd, void *buffer, size_t length, off_t offset)
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

static int copy_range(int input, int output, uint64_t offset, uint64_t size)
{
    unsigned char buffer[COPY_BUFFER_SIZE];

    while (size > 0) {
        size_t wanted = size < sizeof(buffer) ? (size_t)size : sizeof(buffer);
        if (offset > (uint64_t)INT64_MAX ||
            pread_all(input, buffer, wanted, (off_t)offset) < 0 ||
            write_all(output, buffer, wanted) < 0)
            return -1;
        offset += wanted;
        size -= wanted;
    }
    return 0;
}

static int read_footer(int fd, struct merge_footer *footer, uint64_t *file_size)
{
    struct stat status;

    if (fstat(fd, &status) < 0 || status.st_size < 0)
        return -1;
    *file_size = (uint64_t)status.st_size;
    if (*file_size < sizeof(*footer))
        return 0;
    if (pread_all(fd, footer, sizeof(*footer),
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

static int validate_elf64(const char *path, uint64_t *size)
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
        pread_all(fd, &header, sizeof(header), 0) < 0 ||
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

static void set_payload_name(char destination[256], const char *path)
{
    const char *name = program_name(path);
    snprintf(destination, 256, "%s", name);
}

static int create_bundle(const char *first_path, const char *second_path,
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

    first_fd = validate_elf64(first_path, &first_size);
    if (first_fd < 0)
        goto done;
    second_fd = validate_elf64(second_path, &second_size);
    if (second_fd < 0)
        goto done;
    self_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (self_fd < 0 || fstat(self_fd, &self_status) < 0 || self_status.st_size < 0) {
        fprintf(stderr, "merge: cannot read the binder executable: %s\n", strerror(errno));
        goto done;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", output_path,
                 (long)getpid()) >= (int)sizeof(temporary)) {
        fprintf(stderr, "merge: output path is too long\n");
        goto done;
    }
    output_fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
    if (output_fd < 0) {
        fprintf(stderr, "merge: cannot create '%s': %s\n", temporary, strerror(errno));
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

    if (copy_range(self_fd, output_fd, 0, footer.first_offset) < 0 ||
        copy_range(first_fd, output_fd, 0, first_size) < 0 ||
        copy_range(second_fd, output_fd, 0, second_size) < 0 ||
        write_all(output_fd, &footer, sizeof(footer)) < 0 ||
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

static int run_payload(int bundle_fd, uint64_t offset, uint64_t size,
                       const char *name, int argc, char **argv)
{
    char **child_argv;
    int payload_fd;
    pid_t child;
    int status;

    payload_fd = memfd_create(name, MFD_CLOEXEC);
    if (payload_fd < 0 || copy_range(bundle_fd, payload_fd, offset, size) < 0) {
        fprintf(stderr, "merge: cannot load embedded program '%s': %s\n", name,
                strerror(errno));
        if (payload_fd >= 0)
            close(payload_fd);
        return EXIT_FAILURE;
    }
    child_argv = calloc((size_t)argc + 1U, sizeof(*child_argv));
    if (child_argv == NULL) {
        fprintf(stderr, "merge: out of memory\n");
        close(payload_fd);
        return EXIT_FAILURE;
    }
    child_argv[0] = (char *)name;
    for (int index = 1; index < argc; ++index)
        child_argv[index] = argv[index];

    child = fork();
    if (child == 0) {
        fexecve(payload_fd, child_argv, environ);
        fprintf(stderr, "merge: cannot execute embedded program '%s': %s\n", name,
                strerror(errno));
        _exit(126);
    }
    free(child_argv);
    close(payload_fd);
    if (child < 0) {
        fprintf(stderr, "merge: cannot start embedded program '%s': %s\n", name,
                strerror(errno));
        return EXIT_FAILURE;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "merge: cannot wait for '%s': %s\n", name,
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return EXIT_FAILURE;
}

static int run_bundle(int self_fd, const struct merge_footer *footer,
                      int argc, char **argv)
{
    int first_status = run_payload(self_fd, footer->first_offset,
                                   footer->first_size, footer->first_name,
                                   argc, argv);
    int second_status = run_payload(self_fd, footer->second_offset,
                                    footer->second_size, footer->second_name,
                                    argc, argv);

    return second_status != 0 ? second_status : first_status;
}

static void print_usage(const char *argv0)
{
    printf("Welcome to the merge program.\n");
    printf("Usage: %s source-binary1 source-binary2 -o output-binary\n", argv0);
}

int main(int argc, char **argv)
{
    struct merge_footer footer;
    uint64_t self_size;
    int self_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    int footer_state;

    if (self_fd < 0) {
        fprintf(stderr, "merge: cannot open /proc/self/exe: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    footer_state = read_footer(self_fd, &footer, &self_size);
    if (footer_state < 0) {
        fprintf(stderr, "merge: invalid bundle metadata: %s\n", strerror(errno));
        close(self_fd);
        return EXIT_FAILURE;
    }
    if (footer_state == 1) {
        int status = run_bundle(self_fd, &footer, argc, argv);
        close(self_fd);
        return status;
    }
    close(self_fd);

    if (argc == 1) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc != 5 || strcmp(argv[3], "-o") != 0 || argv[4][0] == '\0') {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    return create_bundle(argv[1], argv[2], argv[4]);
}
