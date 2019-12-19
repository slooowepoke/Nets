#include "common.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

int recv_cmd(void *buf, size_t len, struct sockaddr_in *adr, uint32_t *cmd) {
    socklen_t adrlen = sizeof *adr;
    int res = recvfrom(fd, buf, len, MSG_WAITALL, (struct sockaddr *)adr, &adrlen);
    if (res < 0) {
        perror("recv_cmd failed");
        return -1;
    } else if (res == 0) {
        return -1;
    }
    if (res < 4) {
        return -1;
    }
    memcpy(cmd, buf, 4);
    *cmd = ntohl(*cmd);
    return res;
}

void decode_cmd(void *buf, uint32_t *cmd) {
    memcpy(cmd, buf, 4);
    *cmd = ntohl(*cmd);
}


