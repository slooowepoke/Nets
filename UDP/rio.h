#ifndef RIO_H
#define RIO_H

#include <sys/types.h>
#include <stddef.h>
#include <inttypes.h>

ssize_t rio_readn(int fd, void *buf, size_t n);

ssize_t rio_writen(int fd, void *buf, size_t n);

// *buf should be manually disposed with free()
int rcv_msg(int fd, void **buf, uint32_t *cnt);

int snd_msg(int fd, void *buf, uint32_t cnt);

int snd_number(int fd, int32_t number);

int rcv_number(int fd, int32_t *number);

int snd_number_u(int fd, uint32_t number);

int rcv_number_u(int fd, uint32_t *number);

#endif

