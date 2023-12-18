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

static void do_someting(int connfd) {
    char buf[64] = {};
    int n = read(connfd, buf, sizeof(buf) - 1);
    if(n < 0) {
        fprintf(stderr, "[read] error\n");
        abort();
    }

    printf("client say: %s\n", buf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

int main() {
    // init
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        Die("socket()");
    }

    // set 
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = ntohs(1234);
    address.sin_addr.s_addr = ntohl(0);
    int is_success = bind(fd, (const sockaddr *)&address, sizeof(address));
    if (is_success) {
        Die("bind()");
    }

    // listen
    is_success = listen(fd, SOMAXCONN);
    if(is_success) {
        Die("listen()");
    }

    while(1) {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
        if(connfd < 0) {
            continue; // error
        }
        do_someting(connfd);
        close(connfd);
    }
    return 0;
}