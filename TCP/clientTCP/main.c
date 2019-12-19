#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include "etcp.h"
#include "strnstr.c"


const int BUF_SIZE = 256;
const int CHECK_COUNT = 100;

char* itoaa(int num, char *str, int radix)
{
    if(str == NULL)
    {
        return NULL;
    }
    sprintf(str, "%d", num);
    return str;
}

int getSimpleNum(int from, int *resultArray) {

    int t, i, count = 0, flag;
    if (from <= 0) {
        return -1;
    } else {
        for (t = from; t <= (CHECK_COUNT + from); t++) {
            flag = 1;
            for (i = 2; i * i <= t; i++) {
                if (t % i == 0) {
                    flag = 0;
                    break;
                }
            }
            if (flag) {
                resultArray[count] = t;
                count++;
            }
        }
    }
    return count;
}

void checkSuccess(int s, char *buf) {
    if (recv(s, buf, BUF_SIZE, 0) < 0) {
        printf("Error on receiving message");
    }
    printf("%s", buf);
}

int main() {
    struct sockaddr_in peer;
    int s;
    int rc;
    char buf[BUF_SIZE];
    int port = 7500;
    char addr[15] = "127.0.0.1";

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
        bzero(buf, sizeof(buf));
        printf("\nPlease enter the message: ");
        fgets(buf, sizeof(buf) - 1, stdin);
        if (strlen(buf) > 1) { // проверка на то, чтоб сообщение было не пустым
            if (write(s, buf, BUF_SIZE) <= 0) {
                printf("Error on sending message");
                break;
            };

            if (strnstr(buf, "count", 5) != NULL) {
                checkSuccess(s, buf);
                if (strstr(buf, "400") != NULL) {
                    bzero(buf, BUF_SIZE);
                } else {
                    bzero(buf, BUF_SIZE);

                    if (read(s, buf, BUF_SIZE) <= 0) {
                        printf("Error on receiving message");
                        break;
                    };
                    printf("%s\n", buf);

                    int arrayRes[CHECK_COUNT];
                    int a = atoi(buf);
                    int res = 0;
                    res = getSimpleNum(a, arrayRes);
                    char *resArr = (char *) calloc(res, sizeof(char));
                    char* curI;
                    if (res < 0) {
                        return -1;
                    } else {
                        for (int i = 0; i < res; i++) {
                            itoaa(arrayRes[i], curI, 10);
                            strcat(resArr, curI);
                            strcat(resArr, " ");
                        }
                    }
                    if (write(s, resArr, strlen(resArr)) <= 0) {
                        printf("Error on sending message");
                        break;
                    };

                }
            } else {
                checkSuccess(s, buf);
                if (strnstr(buf, "400", 3) == NULL) {
                    bzero(buf, BUF_SIZE);
                    if (read(s, buf, BUF_SIZE) <= 0) {
                        printf("Error on receiving message");
                        break;
                    };
                    printf("%s\n", buf);
                    bzero(buf, BUF_SIZE);
                } else {
                    bzero(buf, BUF_SIZE);
                }
            }
        } else {
        }
        bzero(buf, BUF_SIZE);
    }

    shutdown(s, 2);
    close(s);

    exit(0);


}
