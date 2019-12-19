#include "rio.h"
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdlib.h>

ssize_t rio_readn(int fd, void *buf, size_t n) {
	size_t left = n;
	ssize_t nread;
	char *pbuf = buf;
	while (left > 0) {
		nread = read(fd, pbuf, left);
		if (nread < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (nread == 0) break;
		left -= nread;
		pbuf += nread;
	}
	return n - left;
}

ssize_t rio_writen(int fd, void *buf, size_t n) {
	size_t left = n;
	ssize_t nwrite;
	char *pbuf = buf;
	while (left > 0) {
		nwrite = write(fd, pbuf, left);
		if (nwrite <= 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		left -= nwrite;
		pbuf += nwrite;
	}
	return n;
}

// *buf should be manually disposed with free()
// returns 0 if EOF and buffer should not be disposed in this case
int rcv_msg(int fd, void **buf, uint32_t *cnt) {
	ssize_t nread;
	uint32_t n;
	nread = rio_readn(fd, &n, 4);
	if (nread == 0) {
		return 0;
	}
	if (nread != 4) {
		return -1;
	}
	n = ntohl(n);
	char *data = malloc(n + 1);
	nread = rio_readn(fd, data, n);
	if (nread == 0) {
		free(data);
		return 0;
	}
	if (nread != n) {
		return -1;
	}
	data[n] = '\0';
	*buf = data;
	*cnt = n;
	return 1;
}

int snd_msg(int fd, void *buf, uint32_t cnt) {
	uint32_t network_cnt = htonl(cnt);
	ssize_t nwrite;
	nwrite = rio_writen(fd, &network_cnt, 4);
	if (nwrite != 4) {
		return -1;
	}
	nwrite = rio_writen(fd, buf, cnt);
	if (nwrite != cnt) {
		return -1;
	}
	return 0;
}

int snd_number_u(int fd, uint32_t number) {
	uint32_t network_number = htonl(number);
	ssize_t nwrite;
	nwrite = rio_writen(fd, &network_number, 4);
	if (nwrite != 4) {
		return -1;
	}
	return 0;
}

// returns -1 if error and errno is set appropriately
// returns 0 if EOF
// returns 4 on success and *number set to the value received from the network
int rcv_number_u(int fd, uint32_t *number) {
	ssize_t nread;
	uint32_t num;
	nread = rio_readn(fd, &num, 4);
	if (nread == 0) {
		return 0;
	}
	if (nread != 4) {
		return -1;
	}
	*number = ntohl(num);
	return nread;
}

int snd_number(int fd, int32_t number) {
	return snd_number_u(fd, (uint32_t) number);
}

int rcv_number(int fd, int32_t *number) {
	return rcv_number_u(fd, (uint32_t*) number);
}



