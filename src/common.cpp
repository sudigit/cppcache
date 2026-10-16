#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include "common.h"

void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}