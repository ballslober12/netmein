/* Minimal setuid-root "su": when the file has the setuid bit (chmod 4755),
 * euid starts as root.  We setuid(0) fully and drop into an interactive
 * root shell.  Fully static - no SDL, no nc4, no external libs. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <grp.h>
#include <errno.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Ensure real uid is root too (not just euid from setuid bit) */
    if (setuid(0) != 0) {
        /* if we can't setuid, we're probably not setuid-root */
    }
    setgid(0);
    setresuid(0, 0, 0);
    setresgid(0, 0, 0);
    setgroups(0, NULL);

    if (geteuid() != 0) {
        printf("su: not root!\n");
        return 1;
    }

    execl("/bin/sh", "sh", "-i", (char *)0);
    execl("/bin/busybox", "sh", "-i", (char *)0);
    perror("su: exec");
    _exit(127);
}