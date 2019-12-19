#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include "rio.h"
#include "common.h"

#define PRIMARIES_FILE_NAME "primaries.txt"
#define LAST_PRIMARIES_MAX_CNT 500
#define MAX_CLIENT_CNT 100

typedef struct {
    uint32_t ip;
    uint16_t port;
    pthread_t tid;
    int pipefd;
} ClientInfo;

ClientInfo clients[MAX_CLIENT_CNT];
int client_cnt = 0;

uint32_t next_rng_start = 1;
uint32_t delta = 100;
uint32_t last_primaries[LAST_PRIMARIES_MAX_CNT];
int last_cnt = 0;
int last_start = 0;
pthread_mutex_t delta_mutex, last_mutex, clients_mutex;

typedef struct {
    int pipefd;
    struct sockaddr_in adr;
} TInfo;

void send_kill(const struct sockaddr_in *adr);
void *start(void *arg);
void send_it(const uint32_t *buf, size_t len, const struct sockaddr_in *adr);

void add_client(const struct sockaddr_in *adr) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_cnt; i++) {
        if (!memcmp(&clients[i].ip, &adr->sin_addr.s_addr, 4) &&
            !memcmp(&clients[i].port, &adr->sin_port, 2)) {
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }
    if (client_cnt >= MAX_CLIENT_CNT) {
        fprintf(stderr, "Failed to add client to list because list is full\n");
        pthread_mutex_unlock(&clients_mutex);
        return;
    }
    clients[client_cnt].ip = adr->sin_addr.s_addr;
    clients[client_cnt].port = adr->sin_port;
    pthread_t tid;
    TInfo *info = (TInfo *) malloc(sizeof(TInfo));
    int pipefds[2];
    if (pipe(pipefds) < 0) {
        perror("pipe failed");
        exit(EXIT_FAILURE);
    }
    info->pipefd = pipefds[0];
    info->adr = *adr;
    if (pthread_create(&tid, NULL, start, (void*)info)) {
        fprintf(stderr, "pthread_create failed\n");
        exit(EXIT_FAILURE);
    }
    clients[client_cnt].tid = tid;
    clients[client_cnt].pipefd = pipefds[1];
    client_cnt++;
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(const struct sockaddr_in *adr) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_cnt; i++) {
        if (!memcmp(&clients[i].ip, &adr->sin_addr.s_addr, 4) &&
            !memcmp(&clients[i].port, &adr->sin_port, 2)) {
            ClientInfo tmp_client = clients[i];
            clients[i] = clients[client_cnt - 1];
            clients[client_cnt - 1] = tmp_client;
            client_cnt--;
            pthread_mutex_unlock(&clients_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void show_clients() {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_cnt; i++) {
        char ip_str[30];
        if (!inet_ntop(AF_INET, &clients[i].ip, ip_str, 30)) {
            perror("inet_ntop failed");
            continue;
        }
        uint16_t port = ntohs(clients[i].port);
        printf("%d\t%s:%" PRIu16 "\n", i, ip_str, port);
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client_by_index(int client_index) {
    if (client_index < 0 || client_index >= MAX_CLIENT_CNT) {
        fprintf(stderr, "Bad client index\n");
        return;
    }
    pthread_mutex_lock(&clients_mutex);
    struct sockaddr_in adr = {0};
    adr.sin_family = AF_INET;
    adr.sin_addr.s_addr = clients[client_index].ip;
    adr.sin_port = clients[client_index].port;
    send_kill(&adr);
    ClientInfo tmp_client = clients[client_index];
    clients[client_index] = clients[client_cnt - 1];
    clients[client_cnt - 1] = tmp_client;
    client_cnt--;
    pthread_mutex_unlock(&clients_mutex);
}

void kill_all_clients() {
    pthread_mutex_lock(&clients_mutex);
    struct sockaddr_in adr = {0};
    adr.sin_family = AF_INET;
    for (int i = 0; i < client_cnt; i++) {
        adr.sin_addr.s_addr = clients[i].ip;
        adr.sin_port = clients[i].port;
        send_kill(&adr);
    }
    client_cnt = 0;
    pthread_mutex_unlock(&clients_mutex);
}

void send_last(const struct sockaddr_in *adr, uint32_t n, uint32_t index, uint32_t number);

void add_primary(uint32_t value) {
    pthread_mutex_lock(&last_mutex);
    if (last_cnt >= LAST_PRIMARIES_MAX_CNT) {
        last_primaries[last_start] = value;
        last_start = (last_start + 1) % LAST_PRIMARIES_MAX_CNT;
    } else {
        last_primaries[last_cnt] = value;
        last_cnt++;
    }
    FILE *f = fopen(PRIMARIES_FILE_NAME, "a+");
    fprintf(f, "%" PRIu32 " ", value);
    fclose(f);
    pthread_mutex_unlock(&last_mutex);
}

void send_max(const struct sockaddr_in *adr) {
    pthread_mutex_lock(&last_mutex);
    int cnt = LAST_PRIMARIES_MAX_CNT;
    if (cnt > last_cnt) {
        cnt = last_cnt;
    }
    uint32_t buf[3];
    int ind = (last_start + last_cnt - 1) % LAST_PRIMARIES_MAX_CNT;
    buf[0] = htonl(CMD_LAST);
    buf[1] = htonl(1);
    uint32_t max_value = 0;
    while (cnt > 0) {
        if (last_primaries[ind] > max_value) {
            max_value = last_primaries[ind];
        }
        ind--;
        if (ind < 0) {
            ind = LAST_PRIMARIES_MAX_CNT - 1;
        }
        cnt--;
    }
    pthread_mutex_unlock(&last_mutex);
    buf[2] = htonl(max_value);
    send_it(buf, sizeof(uint32_t) * 3, adr);
}

void send_last_primaries(const struct sockaddr_in *adr, int cnt, int want_max) {
    if (want_max) {
        send_max(adr);
        return;
    }
    pthread_mutex_lock(&last_mutex);
    if (cnt > last_cnt) {
        cnt = last_cnt;
    }
    uint32_t n_real = (uint32_t) cnt;
    uint32_t *buf = (uint32_t *) malloc(sizeof(uint32_t) * (n_real + 2));
    int ind = (last_start + last_cnt - 1) % LAST_PRIMARIES_MAX_CNT;
    buf[0] = htonl(CMD_LAST);
    buf[1] = htonl(n_real);
    while (cnt > 0) {
        buf[cnt + 1] = htonl(last_primaries[ind]);
        // printf("%" PRIu32 "\n", last_primaries[ind]);
        ind--;
        if (ind < 0) {
            ind = LAST_PRIMARIES_MAX_CNT - 1;
        }
        cnt--;
    }
    pthread_mutex_unlock(&last_mutex);
    // printf("%" PRIu32 " - %d\n", n_real, (int)(sizeof(uint32_t) * (n_real+2)));
    send_it(buf, sizeof(uint32_t) * (n_real+2), adr);
    free(buf);
}

void send_it(const uint32_t *buf, size_t len, const struct sockaddr_in *adr) {
    int res = sendto(fd, (void *) buf, len, MSG_CONFIRM, (struct sockaddr *) adr, sizeof *adr);
    if (res < 0) {
        perror("send_it failed");
    }
    // printf("%d =)\n", res);
}

void send_count(const struct sockaddr_in *adr, uint32_t from, uint32_t to) {
    uint32_t data[] = {
        htonl(CMD_COUNT),
        htonl(from),
        htonl(to)
    };
    send_it(data, sizeof data, adr);
}

void send_last(const struct sockaddr_in *adr, uint32_t n, uint32_t index, uint32_t number) {
    uint32_t data[] = {
        htonl(CMD_LAST),
        htonl(n),
        htonl(index),
        htonl(number)
    };
    send_it(data, sizeof data, adr);
}

void send_kill(const struct sockaddr_in *adr) {
    uint32_t cmd = htonl(CMD_KILL);
    send_it(&cmd, sizeof cmd, adr);
}

void proc_count(const struct sockaddr_in *adr) {
    pthread_mutex_lock(&delta_mutex);
    uint32_t start = next_rng_start;
    uint32_t end = start + delta - 1;
    pthread_mutex_unlock(&delta_mutex);
    send_count(adr, start, end);
}

void proc_last(const struct sockaddr_in *adr, const char *buf, ssize_t len) {
    if (len < 8) {
        return;
    }
    uint32_t n, want_max;
    memcpy(&n, buf, 4);
    memcpy(&want_max, buf + 4, 4);
    n = ntohl(n);
    want_max = ntohl(want_max);
    send_last_primaries(adr, n, want_max);
}

int everything_received(CountCell *count_buf) {
    for (int i = 0; i < MAXCOUNTBUF; i++) {
        if (count_buf[i].received == 0) {
            return 0;
        }
        if (count_buf[i].value == MAX_VAL) {
            return 1;
        }
    }
    return 1;
}

void add_to_file(CountCell *count_buf) {
    for (int i = 0; i < MAXCOUNTBUF; i++) {
        if (count_buf[i].value == MAX_VAL) {
            return;
        }
        add_primary(count_buf[i].value);
    }
}

void proc_count_res(const struct sockaddr_in *adr, const char *buf, ssize_t len,
        uint32_t *offset, CountCell *count_buf) {
    (void) adr;
    if (len < 16) {
        return;
    }
    uint32_t from, to, index, value;
    memcpy(&from, buf, 4);
    memcpy(&to, buf + 4, 4);
    memcpy(&index, buf + 8, 4);
    memcpy(&value, buf + 12, 4);
    from = ntohl(from);
    to = ntohl(to);
    pthread_mutex_lock(&delta_mutex);
    if (to + 1 > next_rng_start) {
        next_rng_start = to + 1;
    }
    pthread_mutex_unlock(&delta_mutex);
    index = ntohl(index);
    value = ntohl(value);
    

    uint32_t data[] = {
        htonl(CMD_COUNT_ACK),
        htonl(from),
        htonl(to),
        htonl(index)
    };
    send_it(data, sizeof data, adr);

    uint32_t req_offset = index / MAXCOUNTBUF * MAXCOUNTBUF;
    if (req_offset == *offset) {
        count_buf[index % MAXCOUNTBUF].value = value;
        count_buf[index % MAXCOUNTBUF].received = 1;
        if (everything_received(count_buf)) {
            add_to_file(count_buf);
            *offset += MAXCOUNTBUF;
            memset(count_buf, 0, sizeof(CountCell) * MAXCOUNTBUF);
        }
    }

}

void proc_kill(const struct sockaddr_in *adr) {
    remove_client(adr);
    printf("Client disconnected\n");
}

void process_cmd(uint32_t cmd, char *buf, ssize_t len, const struct sockaddr_in *adr,
        uint32_t *offset, CountCell *count_buf) {
    switch (cmd) {
        case CMD_COUNT:
            *offset = 0;
            proc_count(adr);
            break;
        case CMD_LAST:
            proc_last(adr, buf, len);
            break;
        case CMD_COUNT_RES:
            proc_count_res(adr, buf, len, offset, count_buf);
            break;
        case CMD_KILL:
            proc_kill(adr);
            break;
    }
}

void *start(void *arg) {
    TInfo *infoAsArg = (TInfo *)arg;
    TInfo info = *infoAsArg;
    free(infoAsArg);
    int pipefd = info.pipefd;
    struct sockaddr_in adr = info.adr;
    uint32_t cmd;
    char *buf;
    uint32_t nread;
    uint32_t offset = 0;
    CountCell count_buf[MAXCOUNTBUF] = { { 0 } };
    while (rcv_msg(pipefd, (void**)&buf, &nread) > 0) {
        decode_cmd(buf, &cmd);
        process_cmd(cmd, buf + 4, nread - 4, &adr, &offset, count_buf);
        free(buf);
    }
    return NULL;
}

void send_to_thread(const struct sockaddr_in *adr, char *buf, int nread) {
    int pipefd = -1;
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_cnt; i++) {
        if (!memcmp(&clients[i].ip, &adr->sin_addr.s_addr, 4) &&
            !memcmp(&clients[i].port, &adr->sin_port, 2)) {
            pipefd = clients[i].pipefd;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    if (pipefd < 0) {
        return;
    }
    snd_msg(pipefd, (void*)buf, (uint32_t)nread);
}

void *start_listen(void *arg) {
    (void) arg;
    char buf[BUFSIZE];
    struct sockaddr_in adr = {0};
    int nread;
    uint32_t cmd;
    while (1) {
        nread = recv_cmd(buf, BUFSIZE, &adr, &cmd);
        if (nread < 0) {
            continue;
        }
        add_client(&adr);
        send_to_thread(&adr, buf, nread);
    }
    return NULL;
}

void start_interactive() {
    char buf[256];
    while (1) {
        printf("Enter command: ");
        fflush(stdout);
        if (!fgets(buf, 256, stdin)) {
            if (ferror(stdin)) {
                perror("fgets failed");
            }
            exit(EXIT_FAILURE);
        }
        int len = strlen(buf);
        if (len <= 0) {
            continue;
        }
        if (buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        if (!strcmp(buf, "list")) {
            show_clients();
        } else if (!strcmp(buf, "kill")) {
            int client_index = 0;
            printf("Whom: ");
            fflush(stdout);
            scanf(" %d", &client_index);
            fgets(buf, 256, stdin);
            remove_client_by_index(client_index);
            printf("Client disconnected and removed\n");
        } else if (!strcmp(buf, "delta")) {
            int new_delta = 0;
            printf("New value: ");
            fflush(stdout);
            scanf(" %d", &new_delta);
            fgets(buf, 256, stdin);
            if (new_delta < 0 || new_delta > 1000000000) {
                printf("bad value\n");
            } else {
                pthread_mutex_lock(&delta_mutex);
                delta = new_delta;
                pthread_mutex_unlock(&delta_mutex);
            }
        } else if (!strcmp(buf, "exit")) {
            kill_all_clients();
            sleep(1);
            exit(EXIT_FAILURE);
        } else {
            puts("Unknown command");
            puts("Use \"list\", \"kill\", \"delta\" or \"exit\"");
        }
    }
}

void init_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_init(mutex, NULL)) {
        fprintf(stderr, "pthread_mutex_init failed\n");
        exit(EXIT_FAILURE);
    }
}

int main() {
    init_mutex(&delta_mutex);
    init_mutex(&last_mutex);
    init_mutex(&clients_mutex);
    FILE *f = fopen(PRIMARIES_FILE_NAME, "w");
    if (f == NULL) {
        perror("fopen failed");
        exit(EXIT_FAILURE);
    }
    fclose(f);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_adr = {0};
    server_adr.sin_family = AF_INET;
    server_adr.sin_port = htons(7777);
    int res = bind(fd, (const struct sockaddr *) &server_adr, sizeof server_adr);
    if (res < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    pthread_t tid;
    if (pthread_create(&tid, NULL, start_listen, NULL)) {
        fprintf(stderr, "pthread_create failed\n");
        exit(EXIT_FAILURE);
    }
    start_interactive();
    if (pthread_join(tid, NULL)) {
        fprintf(stderr, "pthread_join failed\n");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_destroy(&delta_mutex);
    pthread_mutex_destroy(&last_mutex);
    pthread_mutex_destroy(&clients_mutex);
}

