#pragma once
#include <stddef.h>

const size_t k_max_msg = 32<<20;

void msg(const char *msg);

void msg_errno(const char *msg);

void die(const char *msg);