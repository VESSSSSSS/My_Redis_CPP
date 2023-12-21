#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

const int k_max_msg = 4096;

// 每个fd，也就是每个连接的socket之间的状态
struct Conn {
    int fd = -1;
    uint32_t state = 0;

    //buffer for reading
    uint8_t rbuf[4 + k_max_msg]; // 头部 + 内存中一个页面长度 + '\0'
    size_t rbuf_size;

    //buffer for write
    uint8_t wbuf[4 + k_max_msg];
    size_t wbuf_size;
    size_t wbuf_sent; // 成功送出的字节数
};

enum { // mark the connection for deletion
    STATE_REQ = 0, // read 处于请求阶段
    STATE_RES = 1, // write  这个状态表示连接处于响应阶段
    STATE_END = 2,  // 这个状态标记了连接待删除的状态
}

void
Die(char *msg){
    fprintf(sterr, "error : %s", msg);
    abort();
}

void msg(char *msg) {
    fprintf(sterr, "%s", msg);
}

void state_req(Conn* conn) {
    while(try_fill_buffer(conn)) // 尝试尽量将读入缓冲区填满
        ;
}

void state_res(Conn* conn) {
    while(try_flush_buffer(conn))
        ;
}
bool try_fill_buffer(Conn* conn) {
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);

    if(rv < 0 && errno == EAGAIN) {
        // EAGAIN 表示读取操作暂时无法进行，需稍后再操作
        return false;
    }
    
    if(rv < 0) {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if(rv == 0) {
        if(conn -> rbuf_size > 0) {
            msg("unexpected EOF");
        } else {
            msg("EOF");
        }

        conn -> state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    
    // 用于处理请求的管道化，也就是不必等待每个请求的响应再发送下一个请求，
    // 而是可以直接发送多个请求，然后按照顺序等待响应。
    while(try_one_request(conn)) {} 
    return (conn->state == STATE_REQ);
}

bool try_one_request(Conn* conn) {
    if(conn->rbuf_size < 4) {
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if(len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }

    if(4 + len > conn->rbuf_size) {
        return false;
    }

     // got one request, do something with it
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // generating echoing response
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}
bool try_flush_buffer(Conn* conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        // response was fully sent, change state back
        conn->state = STATE_REQ; // 注意每次连接的状态改变，依据写（读）缓冲区的数据情况
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}
void connect_io(Conn* conn) {
    if(conn -> state == STATE_REQ) {
        state_req(conn); // 读取一个请求
    } else if (conn->state == STATE_RES) {
        state_res(conn); // 回复一个请求
    } else {
        assert(0); // 退出，未知错误
    }
}

void accept_new_conn(vector<Conn*> fd2conn , int fd) {
    struct sockaddr_in client_addr = {}; // 因特网的套接字的地址结构
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        msg("accept() error");
        return -1;  // error
    }

    fd_set_nb(connfd);

    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if(!conn) {
        close(connfd);
        return -1;
    }

    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        Die("socket()");
    }
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    
    //bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1233);
    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));

    int rv = listen(fd, SOMAXCONN);
    if(rv) {
        Die("rv()");
    }

    // 全部的 client 连接
    std::vector<Conn *> fd2conn;

    //设置listen fd（socket） 为nonblocking mode
    fd_set_nb(fd);

    //the event loop
    std::vector<struct pollfd> poll_args;
    while(1) {
        poll_args.clear();

        struct pollfd poll_fd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for(Conn* conn: fd2conn) {
            if(!conn) { // null
                continue;
            }

            struct pollfd poll_fd = {};
            poll_fd.fd = conn->fd;
            poll_fd.events = (conn->state ? POLLOUT : POLLIN);
            poll_fd.events |= POLLERR; // 用于使用poll函数监视错误事件，及时处理连接状态的异常情况
            poll_args.push_back(poll_fd);
        }
    }
    // 使用poll函数对于所有fd进行轮询
    // 通过poll函数，将revents状态设置成实际发生的事件
    // events则是期待发生的事件（对此连接）
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);

    // 返回值rv是实际就绪的事件
    if(rv < 0) {
        Die("poll()");
    }

    // 处理实际就绪连接
    for (size_t i = 1; i < poll_args.size();i ++ ) {
        if(poll_args[i].revents) {
            Conn *conn = fd2conn[poll_args[i].fd];
            connect_io(conn);
            if(conn->state==STATE_END) {
                fd2conn[conn->fd] = NULL; // 这里的文件描述符的数据结构不需要释放，是因为fd还要继续接着用
                close(conn->fd);
                free(conn);
            }
        }
    }

    if (poll_args[0].revents) {
        accept_new_conn(fd2conn, fd);
    }

    // revents 是 pollfd 结构体中的一个字段，表示事件的发生情况。
    // 如果 revents 不为0，说明相应的事件发生了。

    // 在这里，代码检查的是监听套接字（listening socket）是否有活动，
    // 即是否有新的连接请求到达。
}