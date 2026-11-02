/* A small Unix pipe example: parent writes, child reads until EOF.
 *
 * Build and run inside Linux 0.11 or another Unix-like system:
 *   gcc -O pipe_test.c -o pipe_test
 *   ./pipe_test
 *
 * pipe() creates two descriptors: fd[0] reads, fd[1] writes.
 * fork() gives both processes their own copies of both descriptors.
 *
 *   Parent: close(fd[0]); write(fd[1], ...); close(fd[1]);
 *   Child:  close(fd[1]); read(fd[0], ...) until EOF; close(fd[0]);
 *
 * read() on an empty pipe waits while any write end remains open.
 * Once all write ends are closed, buffered data can still be read.
 * After that data is consumed, read() returns 0: EOF.
 * close() releases one descriptor; it does not erase buffered data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

static void fail(const char *operation)
{
    perror(operation);
    exit(1);
}

void read_from_pipe(int fd[2])
{
    int n, total = 0;
    char buffer[8];

    /* The child only reads. Keeping its write end open would prevent
     * EOF even after the parent closes its own write end. */
    close(fd[1]);

    printf("child: received message: \"");
    for (;;) {
        /* Read at most 8 bytes. Pipes are byte streams: one write does
         * not have to correspond to one read. No NUL is added by read. */
        n = read(fd[0], buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fail("read");
        }
        if (n == 0)
            break; /* No buffered data and no open write ends: EOF. */

        /* Print only the bytes read; fwrite does not need a NUL terminator. */
        fwrite(buffer, 1, n, stdout);
        total += n;
    }

    printf("\"\n");
    printf("child: read returned 0 (EOF), received %d bytes in total\n", total);
    close(fd[0]); /* The child has finished reading. */
}

static void write_to_pipe(int fd[2], const char *message, int length)
{
    int n, sent = 0;

    close(fd[0]); /* The parent only writes; its read end is unused. */
    printf("parent: writing %d bytes\n", length);
    fflush(stdout);
    while (sent < length) {
        n = write(fd[1], message + sent, length - sent);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fail("write");
        }
        if (n == 0) {
            fprintf(stderr, "FAIL: write made no progress\n");
            exit(1);
        }
        sent += n;
    }

    printf("parent: closing its write end; EOF follows when all write ends close\n");
    close(fd[1]); /* Essential: without this, the child's next read waits. */
}

static void wait_for_child(int child)
{
    int n, status;

    do { n = waitpid(child, &status, 0); } while (n < 0 && errno == EINTR);
    if (n < 0)
        fail("waitpid");
    if (status != 0) {
        fprintf(stderr, "FAIL: child exit status = %d\n", status);
        exit(1);
    }
}

int main(void)
{
    int fd[2], child;
    const char message[] = "Hello through the pipe!";
    const int length = sizeof(message) - 1;

    if (pipe(fd) < 0)
        fail("pipe");
    printf("pipe: fd[0]=%d reads; fd[1]=%d writes\n", fd[0], fd[1]);
    fflush(stdout); /* Flush before fork so this line is not printed twice. */

    child = fork();
    if (child < 0)
        fail("fork");
    if (child == 0) {
        read_from_pipe(fd);
    } else {
        write_to_pipe(fd, message, length);
        wait_for_child(child);
        printf("PASS: writer and reader finished; EOF observed\n");
    }
    return 0;
}
