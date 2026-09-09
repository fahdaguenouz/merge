#include "merge.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void merge_print_usage(const char *argv0)
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
    footer_state = merge_read_footer(self_fd, &footer, &self_size);
    if (footer_state < 0) {
        fprintf(stderr, "merge: invalid bundle metadata: %s\n", strerror(errno));
        close(self_fd);
        return EXIT_FAILURE;
    }
    if (footer_state == 1) {
        int status = merge_run_bundle(self_fd, &footer, argc, argv);
        close(self_fd);
        return status;
    }
    close(self_fd);

    if (argc == 1) {
        merge_print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc != 5 || strcmp(argv[3], "-o") != 0 || argv[4][0] == '\0') {
        merge_print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    return merge_create_bundle(argv[1], argv[2], argv[4]);
}
