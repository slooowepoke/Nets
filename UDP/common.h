#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <inttypes.h>
#include <stddef.h>

#define CMD_COUNT 0
#define CMD_LAST 1
#define CMD_COUNT_RES 2
#define CMD_KILL 3
#define CMD_COUNT_ACK 4

#define BUFSIZE 256
#define MAX_VAL 0xFFFFFFFF
#define MAXCOUNTBUF 50

typedef struct {
    uint32_t value;
    int received;
} CountCell;

int recv_cmd(void *buf, size_t len, struct sockaddr_in *adr, uint32_t *cmd);

void decode_cmd(void *buf, uint32_t *cmd);

int fd;

#endif

