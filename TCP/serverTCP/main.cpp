#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <mutex>
#include <iostream>
#include <vector>

#include "etcp.h"
#include <cerrno>
#include <algorithm>
#include <inttypes.h>
#include "rio.h"

#define BUF_SIZE 256
#define NUMBER_OF_READN_SYMBOLS 256
#define NUMBER_OF_CLIENTS 5
#define PORT 7777
#define MAX_VAL 0xFFFFFFFF

#define CURRENT_SIMPLES "current-simples.txt"
#define LAST_MAX_CNT 100

std::mutex myMutex, currentNMutex, bufferMutex, fileMutex;


uint32_t buffer[LAST_MAX_CNT];
int buffer_cnt = 0;
int buffer_start = 0;

struct socketContainer {
    char *addr;
    int port;
    int socket;
    int index;
};

std::vector<socketContainer> arrayOfConnection;

uint32_t currentN = 1;
uint32_t delta = 10000;

typedef struct {
    int accept_socket;
    int client_sockets[NUMBER_OF_CLIENTS];
    char action;
} pthrData;

void closeSocket(int socket) {
    printf("Closing socket %d\n", socket);
    shutdown(socket, SHUT_RDWR);
    close(socket);
}

void writeError(SOCKET fd, const char *errorMsg) {
    char buf[BUF_SIZE];
    sprintf(buf, "400 ERROR %s\n", errorMsg);
    if (snd_msg(fd, buf, strlen(buf)) < 0) {
        perror("ERROR reading from socket");
        closeSocket(fd);
    }
}

/*void writeSuccess(SOCKET fd) {
    const char *msg = "200 SUCCESS\n";
    if (snd_msg(fd, msg, strlen(msg)) == -1) {
        perror("snd_msg failed");
        closeSocket(fd);
    }
}*/

//Функция для обработки клиента в отдельном потоке
void *threadFunction(void *threadData) {
    auto data = (socketContainer *) threadData;
    socketContainer s1 = *data;
    char *result; //Клиентское сообщение
    uint32_t cnt;
    while (true) {
        result = NULL;
        int failedOrNot = rcv_msg(s1.socket, (void**)&result, &cnt);
        if (failedOrNot < 0) {
            perror("ERROR reading from socket");
            break;
        }
        if (failedOrNot == 0) {
            break;
        }
        if (strstr(result, "count") != nullptr) {
            //writeSuccess(s1.socket);
            FILE *file;
            printf("Client %d send message %s", s1.socket, result);
            uint32_t _from, _to, val;
            
            currentNMutex.lock();
            _from = currentN;
            _to = _from + delta - 1;
            currentN = _to + 1;
            currentNMutex.unlock();
            
            
            if (snd_number_u(s1.socket, _from) < 0) {
                perror("ERROR sending to socket");
                break;
            }
            if (snd_number_u(s1.socket, _to) < 0) {
                perror("ERROR sending to socket");
                break;
            }
            bool goout = false;
            while (1) {
                if (rcv_number_u(s1.socket, &val) <= 0) {
                    perror("ERROR reading from socket");
                    goout = true;
                    break;
                }
                if (val == MAX_VAL) {
                    break;
                }
                fileMutex.lock();
                file = fopen(CURRENT_SIMPLES, "a+");
                fprintf(file, "%" PRIu32 " ", val);
                fclose(file);
                fileMutex.unlock();
                bufferMutex.lock();
                if (buffer_cnt >= LAST_MAX_CNT) {
                    buffer[buffer_start] = val;
                    buffer_start = (buffer_start + 1) % LAST_MAX_CNT;
                } else {
                    buffer[buffer_cnt] = val;
                    buffer_cnt++;
                }
                bufferMutex.unlock();
            }
            if (goout) {
                break;
            }
        } else if (strstr(result, "last") || strstr(result, "MAX") || (strstr(result, "max"))) {
            //writeSuccess(s1.socket);
            uint32_t cnt = 1;
            if (strstr(result, "last")) {
                if (rcv_number_u(s1.socket, &cnt) <= 0) {
                    perror("ERROR reading from socket");
                    break;
                }
            }
            bufferMutex.lock();
            if (cnt > buffer_cnt) {
                cnt = buffer_cnt;
            }
            int ind = (buffer_start + buffer_cnt - 1) % LAST_MAX_CNT;
            bool goout = false;
            while (cnt > 0) {
                if (snd_number_u(s1.socket, buffer[ind]) < 0) {
                    perror("failed to send");
                    goout = true;
                    break;
                }
                ind--;
                if (ind < 0) {
                    ind = LAST_MAX_CNT - 1;
                }
                cnt--;
            }
            bufferMutex.unlock();
            if (goout) {
                break;
            }
            uint32_t val = MAX_VAL;
            if (snd_number_u(s1.socket, val) < 0) {
                perror("failed to send");
                break;
            }
        } else {
            writeError(s1.socket, "Incorrect command");
        }
        if (result) {
            free(result);
        }
    }

    myMutex.lock();
    arrayOfConnection.erase(std::remove_if(arrayOfConnection.begin(),
                                           arrayOfConnection.end(),
                                           [&](const socketContainer &rhs) {
                                               return s1.socket == rhs.socket;
                                           }
    ), arrayOfConnection.end());
    myMutex.unlock();
}

//Функция для обработки подключений клиентов
void *acceptThread(void *threadData) {
    auto data = (pthrData *) threadData;
    int client_socket;
    int n;
    listen(data->accept_socket, 5);
    struct sockaddr_in cli_addr;
    unsigned int clilen;
    pthread_t thread;
    while (true) {
        if (arrayOfConnection.size() < NUMBER_OF_CLIENTS) {
            client_socket = accept(data->accept_socket, (struct sockaddr *) &cli_addr, &clilen);
            if (client_socket <= 0) {
                break;
            }
            myMutex.lock();
            socketContainer newSock;
            newSock.addr = inet_ntoa(cli_addr.sin_addr);
            newSock.port = cli_addr.sin_port;
            newSock.socket = client_socket;
            arrayOfConnection.push_back(newSock);
            myMutex.unlock();
            n = pthread_create(&thread, nullptr, threadFunction, &(arrayOfConnection[arrayOfConnection.size() - 1]));
            if (n != 0) {
                printf("ERROR creating client's thread");
                break;
            }
        }
    }

    for (int i = 0; i < arrayOfConnection.size(); i++) {
        closeSocket(arrayOfConnection[i].socket);
        pthread_join(thread, NULL);
    }
}

int main() {

    FILE *file = fopen(CURRENT_SIMPLES, "w+");
    if (!file) {
        perror("fopen failed");
        exit(EXIT_FAILURE);
    }
    fclose(file);

    std::string userCommand;
    struct sockaddr_in local;

    local.sin_family = AF_INET;
    local.sin_port = htons(PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    auto threadData = (pthrData *) malloc(sizeof(pthrData));

    threadData->accept_socket = socket(AF_INET, SOCK_STREAM, 0);
    int val = 1;
    if (setsockopt(threadData->accept_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) == -1) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    if (bind(threadData->accept_socket, (struct sockaddr *) &local, sizeof(local)) < 0) {
        printf("ERROR: port is busy");
        exit(1);
    }
    std::cout << "Server binding to port : " << PORT << std::endl;

    auto thread = (pthread_t *) malloc(sizeof(pthread_t));
    pthread_create(thread, nullptr, acceptThread, threadData);

    while (true) {

        std::cin >> userCommand;

        if (userCommand == "list") {
            printf("list\n");
            printf("Number of connections: %d\n", (int) arrayOfConnection.size());
            for (int i = 0; i < arrayOfConnection.size(); i++) {
                printf("%d. Client address: %s:%d client socket: %d\n", i + 1, arrayOfConnection[i].addr,
                       arrayOfConnection[i].port, arrayOfConnection[i].socket);
            }
        } else if (userCommand == "exit") {
            printf("exit\n");
            break;
        } else if (userCommand == "delta") {
            int newDelta;
            std::cin >> newDelta;
            currentNMutex.lock();
            delta = newDelta;
            currentNMutex.unlock();
            printf("Delta changed to %" PRIu32 "\n", delta);
        } else if (userCommand == "kill") {
            if (arrayOfConnection.empty()) {
                std::cout << "No open connections" << std::endl;
            } else {
                printf("Kill user, enter number of client\n");
                int client_number = -1;

                for (int i = 0; i < arrayOfConnection.size(); i++) {
                    printf("%d. Client address: %s:%d client socket: %d\n", i + 1, arrayOfConnection[i].addr,
                           arrayOfConnection[i].port, arrayOfConnection[i].socket);
                }

                scanf("%d", &client_number);
                closeSocket(arrayOfConnection[client_number - 1].socket);
                printf("The client has been disconnected\n", client_number - 1);
            }
        } else {
            std::cout << userCommand << ": command not found" << std::endl;
        }
    }

    for (int i = 0; i < arrayOfConnection.size(); i++) {
        closeSocket(arrayOfConnection[i].socket);
    }

    shutdown(threadData->accept_socket, SHUT_RDWR);
    close(threadData->accept_socket);

    pthread_join(*thread, NULL);

    free(thread);
    free(threadData);

    //exit( 0 );
    return 0;
}
