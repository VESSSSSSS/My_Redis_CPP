#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

static void Die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

int main() {
    // init
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        Die("socket()");
    }

    // create address
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = ntohs(1234);
    address.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
    int is_success = connect(fd, (const struct sockaddr *)&address, sizeof(address));
    if(is_success) {
        Die("connect()");
    }

    char msg[] = "hello";
    write(fd, msg, strlen(msg));
    char rbuf[64] = {};
    int n = read(fd, rbuf, sizeof(rbuf) - 1);

    if(n < 0) {
        Die("read()");
    }
    printf("server says : %s\n", rbuf);
    close(fd);
    return 0;
}
