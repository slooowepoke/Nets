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

#define NUMBER_OF_READN_SYMBOLS 256
#define NUMBER_OF_CLIENTS 5
#define PORT 7777


std::mutex myMutex;

struct socketContainer {
    char *addr;
    int port;
    int socket;
    int index;
};

std::vector<socketContainer> arrayOfConnection;

int currentN = 1;

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

void writeError(SOCKET fd, char *errorMsg) {
    char buf[NUMBER_OF_READN_SYMBOLS];
    sprintf(buf, "400 ERROR %s\n", errorMsg);
    if (send(fd, buf, strlen(buf), 0) < 0) {
        perror("ERROR reading from socket");
        closeSocket(fd);
    }
}

void writeSuccess(SOCKET fd) {
    char buf[NUMBER_OF_READN_SYMBOLS];
    sprintf(buf, "200 SUCCESS\n");
    if (send(fd, buf, strlen(buf), 0) < 0) {
        perror("ERROR reading from socket");
        closeSocket(fd);
    }
}

int readN(SOCKET fd, char *bp, size_t len) {
    int cnt;
    int rc;

    cnt = len;
    while (cnt > 0) {
        rc = recv(fd, bp, cnt, 0);

        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (rc == 0)
            return len - cnt;
        bp += rc;
        cnt -= rc;
    }
    return len;
}

int getAmountNumbers() {
    int amount = 0;
    FILE *file = fopen("/home/natalia/CLionProjects/serverTCP/current-simples.txt", "a+");
    fseek(file, 0, SEEK_SET);
    while (!feof(file)) {
        if (getc(file) == ' ') {
            amount++;
        }
    }
    amount++;
    fclose(file);
    return amount;
}


char *findLastNSimples(int n) {
    if (n <= getAmountNumbers()) {
        FILE *file = fopen("/home/natalia/CLionProjects/serverTCP/current-simples.txt", "a+");
        fseek(file, 0, SEEK_END);
        fseek(file, -1, SEEK_CUR);
        int count = 0;
        int symbols = 0;
        while (true) {
            fseek(file, -1, SEEK_CUR);
            char l = (char) fgetc(file);
            symbols++;
            fseek(file, -1, SEEK_CUR);
            if (l == ' ') {
                count++;
            }
            if (count == n && file == SEEK_SET) {
                count++;
                break;
            }
            if (count == n) {
                break;
            }
        }
        char *result = (char *) calloc(count, sizeof(char));
        bzero(result, n * symbols + 1);
        fgets(result, n * symbols + 1, file);
        fclose(file);
        return result;
    } else {
        return "Can't read so many simples";
    }
}

//Функция для обработки клиента в отдельном потоке
void *threadFunction(void *threadData) {
    auto data = (socketContainer *) threadData;
    socketContainer s1 = *data;
    char result[NUMBER_OF_READN_SYMBOLS]; //Клиентское сообщение
    //char response[21] = "OK, I've got message";
    memset(result, '\0', 256);
    while (true) {
        if (readN(s1.socket, result, NUMBER_OF_READN_SYMBOLS) < 0) {
            perror("ERROR reading from socket");
            break;
        };
        if (strstr(result, "count") != nullptr) {
            writeSuccess(s1.socket);
            FILE *file;
            printf("Client %d send message %s", s1.socket, result);
            bzero(result, NUMBER_OF_READN_SYMBOLS);
            char curN[10];
            sprintf(curN, "%d", currentN);
            if (write(s1.socket, curN, strlen(curN)) <= 0) {
                perror("ERROR sending to socket");
                break;
            };

            file = fopen("/home/natalia/CLionProjects/serverTCP/current-simples.txt", "a+");
            if (read(s1.socket, result, NUMBER_OF_READN_SYMBOLS) < 0) {
                perror("ERROR reading from socket");
                break;
            }
            fprintf(file, "%s", result);
            fclose(file);
            char *last = findLastNSimples(1);
            currentN = atoi(last) + 1;
            free(last);
            bzero(result, NUMBER_OF_READN_SYMBOLS);
        } else if (strstr(result, "last")) {
            writeSuccess(s1.socket);
            sprintf(result, "%s", "How much?");
            if (write(s1.socket, result, NUMBER_OF_READN_SYMBOLS) < 0) {
                perror("ERROR reading from socket");
                break;
            };
            bzero(result, NUMBER_OF_READN_SYMBOLS);
            if (readN(s1.socket, result, NUMBER_OF_READN_SYMBOLS) < 0) {
                perror("ERROR reading from socket");
                break;
            };
            int n = atoi(result);
            if (n <= getAmountNumbers()) {
                writeSuccess(s1.socket);
                bzero(result, NUMBER_OF_READN_SYMBOLS);
                char *res = findLastNSimples(n);
                sprintf(result, res);
                if (write(s1.socket, result, NUMBER_OF_READN_SYMBOLS) < 0) {
                    perror("ERROR reading from socket");
                    break;
                };
                bzero(result, NUMBER_OF_READN_SYMBOLS);
            } else {
                writeError(s1.socket, "Can't read so many simples");
            }

        } else if (strstr(result, "MAX") || (strstr(result, "max"))){
            writeSuccess(s1.socket);
            sprintf(result, "MAX NUMBER IS %d", currentN - 1);
            if (write(s1.socket, result, NUMBER_OF_READN_SYMBOLS) < 0) {
                perror("ERROR reading from socket");
                break;
            };
            bzero(result, NUMBER_OF_READN_SYMBOLS);
        } else if (strlen(result)) {
            writeError(s1.socket, "Incorrect command");
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
            n = pthread_create(&thread, nullptr, threadFunction, &newSock);
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

    FILE *file = fopen("/home/natalia/CLionProjects/serverTCP/current-simples.txt", "w+");
    fclose(file);

    std::string userCommand;
    struct sockaddr_in local;

    local.sin_family = AF_INET;
    local.sin_port = htons(PORT);
    local.sin_addr.s_addr = INADDR_ANY;

    auto threadData = (pthrData *) malloc(sizeof(pthrData));

    threadData->accept_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(threadData->accept_socket, (struct sockaddr *) &local, sizeof(local)) < 0) {
        printf("ERROR: port is busy");
        exit(1);
    };


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