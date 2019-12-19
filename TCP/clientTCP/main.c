#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <inttypes.h>
#include "etcp.h"
#include "strnstr.c"
#include "rio.h"

#define MAX_VAL 0xFFFFFFFF

const int BUF_SIZE = 256;

void simpleNumbers(int fd, uint32_t _from, uint32_t _to) {
    uint32_t t, i;
    int flag;
    for (t = _from; t <= _to; t++) {
        flag = 1;
        for (i = 2; i * i <= t; i++) {
            if (t % i == 0) {
                flag = 0;
                break;
            }
        }
        if (flag) {
            snd_number_u(fd, t);
            printf("%" PRIu32 " ", t);
        }
    }
    t = MAX_VAL;
    snd_number_u(fd, t);
    printf("\n");
}

int checkSuccess(int fd) {
    char *buf;
    uint32_t cnt;
    if (rcv_msg(fd, (void**)&buf, &cnt) < 0) {
        printf("Error on receiving message");
        exit(EXIT_FAILURE);
    }
    int code = atoi(buf);
    printf("%s", buf);
    free(buf);
    return code;
}

int main() {
    struct sockaddr_in peer;
    int s;
    int rc;
    char buf[BUF_SIZE];
    int port = 7500;
    char addr[15] = "127.0.0.1";
    char *buf2;
    uint32_t cnt2;

    printf("Enter the port:\n");
    scanf("%d", &port);
    printf("Enter the address:\n");
    scanf("%s", addr);
    bzero(buf, BUF_SIZE);
    printf("Connecting to server\n");

    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(addr);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket call failed");
        return 1;
    }

    rc = connect(s, (struct sockaddr *) &peer, sizeof(peer));

    if (rc) {
        perror("connect call failed");
        return 1;
    }
    fgets(buf, sizeof(buf) - 1, stdin);
    while (1) {
        printf("\nPlease enter the message: ");
        fgets(buf, sizeof(buf) - 1, stdin);
        if (strlen(buf) <= 1) {
            continue;
        }
        if (snd_msg(s, buf, strlen(buf)) < 0) {
            printf("Error on sending message");
            break;
        }
        if (strnstr(buf, "count", 5) != NULL) {
            if (checkSuccess(s) != 400) {
                uint32_t _from, _to;
                if (rcv_number_u(s, &_from) <= 0) {
                    printf("Error on receiving message or EOF");
                    break;
                }
                if (rcv_number_u(s, &_to) <= 0) {
                    printf("Error on receiving message or EOF");
                    break;
                }
                printf("From %" PRIu32 " to %" PRIu32 "\n", _from, _to);
                simpleNumbers(s, _from, _to);
            }
        } else {
            if (checkSuccess(s) != 400) {
                uint32_t val;
                if (strstr(buf, "last")) {
                    printf("How much: ");
                    scanf(" %" PRIu32, &val);
                    char tmp[3];
                    fgets(tmp, 3, stdin);
                    if (snd_number_u(s, val) < 0) {
                        break;
                    }
                }
                while (1) {
                    if (rcv_number_u(s, &val) <= 0) {
                        printf("Error on receiving message");
                        exit(EXIT_FAILURE);
                    }
                    if (val == MAX_VAL) {
                        printf("\n");
                        break;
                    }
                    printf("%" PRIu32 " ", val);
                }
            }
        }
    }

    shutdown(s, 2);
    close(s);

    exit(0);


}
