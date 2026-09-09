#include "merge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int run_payload(int bundle_fd, uint64_t offset, uint64_t size,
                       const char *name, int argc, char **argv)
{
    char **child_argv;
    int payload_fd;
    pid_t child;
    int status;

    payload_fd = memfd_create(name, MFD_CLOEXEC);
    if (payload_fd < 0 ||
        merge_copy_range(bundle_fd, payload_fd, offset, size) < 0) {
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

int merge_run_bundle(int self_fd, const struct merge_footer *footer,
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
