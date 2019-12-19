#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/select.h>
#include <errno.h>
#include "common.h"

#define ATTEMPT_CNT 10
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct sockaddr_in server_adr;

void prompt();
int send_it(const uint32_t *buf, size_t len);
int send_count();
int send_last(uint32_t n, int want_max);
int send_count_res(uint32_t from, uint32_t to, uint32_t index, uint32_t number);
int send_kill();
void simple_numbers(uint32_t _from, uint32_t _to);
void proc_count(const char *buf, ssize_t len);
void proc_last(const char *buf, ssize_t len);
void proc_kill();
void process_cmd(uint32_t cmd, char *buf, ssize_t len);
void failed_attempt(int attempt_index);
void kill_myself();
void failed_all();

void shuffle(uint32_t* arr, int N)
{
    // инициализация генератора случайных чисел
    srand(time(NULL));
 
    // реализация алгоритма перестановки
    for (int i = N - 1; i >= 1; i--)
    {
        int j = rand() % (i + 1);
 
        int tmp = arr[j];
        arr[j] = arr[i];
        arr[i] = tmp;
    }
}

int Select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
        struct timeval *timeout) {
    int rc;
    while (1) {
        rc = select(n, readfds, writefds, exceptfds, timeout);
        if (rc != -1) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        perror("select()");
        exit(EXIT_FAILURE);
    }
    return rc;
}

void prompt() {
    printf("Enter command: ");
    fflush(stdout);
}

int send_it(const uint32_t *buf, size_t len) {
    int res = sendto(fd, (void *) buf, len, MSG_CONFIRM, (struct sockaddr *) &server_adr, sizeof server_adr);
    if (res < 0) {
        return -1;
    }
    return 0;
}

void failed_attempt(int attempt_index) {
    fprintf(stderr, "warning: failed attempt#%d\n", attempt_index + 1);
    fflush(stderr);
}

void kill_myself() {
    printf("I was killed =(\nBye-bye =(\n");
    exit(EXIT_SUCCESS);
}

void failed_all() {
    fprintf(stderr, "Server was not responding %d times\nBye-bye =(\n", ATTEMPT_CNT);
    exit(EXIT_SUCCESS);
}

int send_count() {
    ssize_t nread;
    uint32_t cmd_res;
    char buf[BUFSIZE];
    struct sockaddr_in adr;
    uint32_t cmd = CMD_COUNT;
    cmd = htonl(cmd);
    for (int i = 0; i < ATTEMPT_CNT; i++) {
        if (send_it(&cmd, sizeof cmd) < 0) {
            fprintf(stderr, "send_it failed\n");
            exit(EXIT_FAILURE);
        }
        nread = recv_cmd(buf, BUFSIZE, &adr, &cmd_res);
        if (nread < 0) {
            failed_attempt(i);
            continue;
        }
        if (cmd_res == CMD_KILL) {
            kill_myself();
        }
        if (cmd_res == CMD_COUNT) {
            process_cmd(cmd_res, buf + 4, nread - 4);
            return 0;
        }
        failed_attempt(i);
    }
    failed_all();
    return 0;
}

int send_last(uint32_t n, int want_max) {
    ssize_t nread;
    uint32_t cmd_res;
    int bufSize = 65536;
    char buf[bufSize];
    struct sockaddr_in adr;
    uint32_t data[] = { htonl(CMD_LAST), htonl(n), htonl(want_max) };
    for (int i = 0; i < ATTEMPT_CNT; i++) {
        if (send_it(data, sizeof data) < 0) {
            fprintf(stderr, "send_it failed\n");
            exit(EXIT_FAILURE);
        }
        nread = recv_cmd(buf, bufSize, &adr, &cmd_res);
        if (nread < 0) {
            failed_attempt(i);
            continue;
        }
        if (cmd_res == CMD_KILL) {
            kill_myself();
        }
        if (cmd_res == CMD_LAST) {
            process_cmd(cmd_res, buf + 4, nread - 4);
            return 0;
        }
        failed_attempt(i);
    }
    failed_all();
    return 0;
}

int send_count_res(uint32_t from, uint32_t to, uint32_t index, uint32_t number) {
    uint32_t data[] = {
        htonl(CMD_COUNT_RES),
        htonl(from),
        htonl(to),
        htonl(index),
        htonl(number)
    };
    return send_it(data, sizeof data);
}

int send_kill() {
    uint32_t cmd = htonl(CMD_KILL);
    return send_it(&cmd, sizeof cmd);
}

int get_count_of_sent(CountCell *count_buf) {
    int cnt = 0;
    for (int i = 0; i < MAXCOUNTBUF; i++) {
        if (count_buf[i].received == 0) {
            cnt++;
        }
        if (count_buf[i].value == MAX_VAL) {
            break;
        }
    }
    return cnt;
}

void send_buf_securely(uint32_t _from, uint32_t _to,
        uint32_t offset, CountCell *count_buf) {
    int cnt;
    int attempts = 0;
    char buf[BUFSIZE];
    uint32_t cmd_res;
    int nread;
    struct sockaddr_in adr;
    while ((cnt = get_count_of_sent(count_buf)) > 0) {
	//shuffle(&count_buf->value, cnt);
        for (int i = 0; i < MAXCOUNTBUF; i++) {
	shuffle(&count_buf->value, cnt);
            if (count_buf[i].received == 0) {
                send_count_res(_from, _to, offset + i, count_buf[i].value);
            }
            if (count_buf[i].value == MAX_VAL) {
                break;
            }
        }
        for (int i = 0; i < cnt; i++) {
            nread = recv_cmd(buf, BUFSIZE, &adr, &cmd_res);
            if (nread < 0) {
                attempts++;
                failed_attempt(i);
                if (attempts >= ATTEMPT_CNT) {
                    failed_all();
                }
                continue;
            }
            if (cmd_res == CMD_KILL) {
                kill_myself();
            }
            if (cmd_res == CMD_COUNT_ACK) {
                if (nread < 4*4) {
                    fprintf(stderr, "strange UDP-packet\n");
                    continue;
                }
                uint32_t from2, to2, index2;
                memcpy(&from2, buf+4, 4);
                memcpy(&to2, buf+8, 4);
                memcpy(&index2, buf+12, 4);
                from2 = ntohl(from2);
                to2 = ntohl(to2);
                index2 = ntohl(index2);
                if (from2 == _from && to2 == _to) {
                    uint32_t offset2 = index2 / MAXCOUNTBUF * MAXCOUNTBUF;
                    if (offset2 == offset) {
                        printf("Value #%" PRIu32 " was received by the server\n", index2);
                        count_buf[index2 % MAXCOUNTBUF].received = 1;
                    }
                }
            }
        }
    }
}

void simple_numbers(uint32_t _from, uint32_t _to) {
    uint32_t t, i;
    int flag;
    uint32_t index = 0;
    CountCell count_buf[MAXCOUNTBUF];
    for (t = _from; t <= _to; t++) {
        flag = 1;
        for (i = 2; i * i <= t; i++) {
            if (t % i == 0) {
                flag = 0;
                break;
            }
        }
        if (flag) {
            // printf("%" PRIu32 " was generated\n", t);
            printf("%" PRIu32 " ", t);
            count_buf[index % MAXCOUNTBUF].value = t;
            count_buf[index % MAXCOUNTBUF].received = 0;
            index++;
            if (index % MAXCOUNTBUF == 0) {
                send_buf_securely(_from, _to, index - MAXCOUNTBUF, count_buf);
            }
            fflush(stdout);
        }
    }
    t = MAX_VAL;
    count_buf[index % MAXCOUNTBUF].value = t;
    count_buf[index % MAXCOUNTBUF].received = 0;
    index++;
    send_buf_securely(_from, _to, (index - 1) / MAXCOUNTBUF * MAXCOUNTBUF, count_buf);
}

void proc_count(const char *buf, ssize_t len) {
    if (len < 8) {
        return;
    }
    uint32_t from, to;
    memcpy(&from, buf, 4);
    memcpy(&to, buf + 4, 4);
    from = ntohl(from);
    to = ntohl(to);
    simple_numbers(from, to);
}

void proc_last(const char *buf, ssize_t len) {
    if (len < 4) {
        return;
    }
    uint32_t n_real;
    memcpy(&n_real, buf, 4);
    n_real = ntohl(n_real);
    if (len < 4 * (1 + n_real)) {
        fprintf(stderr, "proc_last: bad size of buffer =(\n");
        return;
    }
    for (uint32_t i = 0; i < n_real; i++) {
        uint32_t value = ntohl(((const uint32_t *)buf)[i + 1]);
        printf("%" PRIu32 "\n", value);
    }
}

void proc_kill() {
    close(fd);
    exit(0);
}

void process_cmd(uint32_t cmd, char *buf, ssize_t len) {
    switch (cmd) {
        case CMD_COUNT:
            proc_count(buf, len);
            break;
        case CMD_LAST:
            proc_last(buf, len);
            break;
        case CMD_KILL:
            proc_kill();
            break;
    }
}

int main() {
    char ip_addr_str[256];
    printf("Enter IP-address: ");
    fgets(ip_addr_str, 256, stdin);
    int port;
    printf("Enter port: ");
    scanf(" %d", &port);
    char tmp_str[256];
    fgets(tmp_str, 256, stdin);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv);
    memset(&server_adr, 0, sizeof server_adr);
    server_adr.sin_family = AF_INET;
    server_adr.sin_port = htons(port);
    int res = inet_pton(AF_INET, ip_addr_str, & server_adr.sin_addr);
    if (res < 0) {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }
    char buf[BUFSIZE];
    fd_set readfds;
    int n = MAX(STDIN_FILENO, fd) + 1;

    struct sockaddr_in adr3;
    uint32_t cmd3;
    
    prompt();
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(fd, &readfds);
        Select(n, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(fd, &readfds)) {
            if (recv_cmd(buf, BUFSIZE, &adr3, &cmd3) > 0) {
                if (cmd3 == CMD_KILL) {
                    kill_myself();
                }
            }
            
        }
        if (!FD_ISSET(STDIN_FILENO, &readfds)) {
            continue;
        }
        char *res = fgets(buf, BUFSIZE, stdin);
        if (!res) {
            if (ferror(stdin)) {
                perror("fgets failed");
                exit(EXIT_FAILURE);
            } else {
                break;
            }
        }
        size_t cmdlen = strlen(res);
        if (cmdlen == 0) {
            continue;
        }
        if (res[cmdlen - 1] == '\n') {
            res[cmdlen - 1] = '\0';
        }
        if (!strcmp(res, "count")) {
            send_count();
        } else if (!strcmp(res, "last")) {
            printf("How much: ");
            fflush(stdout);
            int n;
            scanf(" %d", &n);
            fgets(res, BUFSIZE, stdin);
            send_last(n, 0);
        } else if (!strcmp(res, "max")) {
            send_last(1, 1);
        } else if (!strcmp(res, "exit")) {
            send_kill();
            break;
        } else {
            printf("Unknown command\n");
        }
	printf("\n");
        prompt();
    }
    close(fd);
    return 0;
}
