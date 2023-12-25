#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>

#include "hashtable.h"
// 一个页面的大小就是4kb，也就是4096字节
const size_t k_max_msg = 4096;
const size_t k_max_args = 1024;

#define container_of(ptr, type, member) ({                  
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    
    (type *)( (char *)__mptr - offsetof(type, member) );})

// 描述连接的状态
enum {
    STATE_REQ = 0,  // read 当前连接的状态为：正在请求读入
    STATE_RES = 1,  // write 当前连接的状态为：正在准备回复客户端的请求
    STATE_END = 2,  // over 当前连接的状态为：连接已经被销毁
};

// 回应的状态
enum {
    RES_OK = 0, // 操作成功的结果状态
    RES_ERR = 1, // 操作失败的结果状态
    RES_NX = 2;  // 未找到的结果状态
}

// 映射一个socket连接，并保存该连接的相应信息，
// 例如：输入、输出缓冲区，以及连接状态等
struct Conn {
    int fd = -1;
    uint32_t state = 0;                               
    // STATE_REQ 代表当前正在读取请求
    // STATE_RES 代表当前正在准备回复

    // buffer for reading
    size_t rbuf_size = 0; // 已经读入缓冲区的大小
    uint8_t rbuf[4 + k_max_msg]; 

    // buffer for writing
    size_t wbuf_size = 0;  // 写缓冲区的总大小
    size_t wbuf_sent = 0;  // 已经写入的大小
    uint8_t wbuf[4 + k_max_msg];
};

void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort(); // 
}

void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

// 设置非堵塞I/O
void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if(errno) {
        die("fcntl error");
    }

    flags |= O_NONBLOCK;

    errno = 0;
    fcntl(fd, F_SETFL, flags); // 设置文件状态未非堵塞I/O
    if(errno) {
        die("fcntl()");
    }
}

void conn_put(std::vector<Conn *> &fd2conn, struct Conn* conn) {
    if(fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }

    fd2conn[conn->fd] = conn;
}

int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if(connfd < 0) {
        msg("accept()");
        return -1;
    }

    // 设置所对应客户端连接为非堵塞I/O
    fd_set_nb(connfd); 
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));

    // error
    if(!conn) {
        close(conn);
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

// 命令都是以每块4字节大小 
// ----|----|----|----|----|----|----|----|
// nstr|len1|str1|len1|str2|... |lenN|strN|
// ----|----|----|----|----|----|----|----|
// 此处nstr为一共有多少个命令字符串
int32_t parse_req(const uint8_t *data , size_t len , std::vector<std::string> &out) {
    if(len < 4) {
        return -1;
    }

    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if(n > k_max_args) {
        return -1;
    }

    size_t pos = 4;
    while(n -- ) {
        if(pos + 4 > len) {
            return -1;
        }

        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if(pos + 4 + sz > len) {
            return -1;
        }

        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += (4 + sz);
    }

    if(pos != len) {
        return -1;
    }

    return 0;
}

// 维护一个关于键的空间 （也就是一个哈希表）
struct {
    HMap db;
} g_data;

// key 的数据结构
struct Entry {
    struct HNode node;
    std::string key;
    std::string val;
};

// 节点是否相同
bool entry_eq(HNode *lhs , HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

// 哈希函数（字符串哈希）
uint64_t str_hash(const uint8_t *data , size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len;i ++ ) {
        h = (h + data[i]) * 0x01000193;
    }

    return h;
}

// 执行 get 命令 => get key
uint32_t do_get(std::vector<std::string> &cmd , uint8_t *res , uint32_t *reslen) {
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);

    if(!node) {
        return RES_NX;
    }

    const std::string &val = container_of(node, Entry, node)->val;
    assert(val.size() <= k_max_msg);
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

// 执行 set 命令 => set key val
uint32_t do_set(std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen) {
    // 不会用到
    (void)res; 
    (void)reslen;

    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if(node) {
        container_of(node, Entry, node)->val.swap(cmd[2]);
    } else {
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->val.swap(cmd[2]);
        ent->node.hcode = key.node.hcode;
        hm_insert(&g_data.db, &ent.node);
    }

    return RES_OK;
}

// 执行 del 命令 => del key 删除
uint32_t do_del(std::vector<std::string> &cmd , uint8_t *res , uint32_t *reslen) {
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);

    if(node) {
        delete container_of(node, Entry, node);
    }

    return RES_OK;
}

bool cmd_is(const std::string &word , const char *cmd) {
    return 0 == strcasecmp(word.c_str(), cmd);
}

int32_t do_request(const uint8_t *req , uint32_t reqlen , uint32_t *rescode , uint8_t *res , uint32_t *reslen) {
    std::vector<std ::string> cmd;
    if(0 != parse_req(req , reqlen , cmd)) {
        msg("error req()");
        return -1;
    }
    if(cmd.size() == 2 && cmd_is(cmd[0] , "get")) {
        *rescode = do_get(cmd, res, reslen);
    } else if(cmd.size() == 3 && cmd_is(cmd[0] , "set")) {
        *rescode = do_set(cmd, res, reslen);
    } else if(cmd.size() == 2 && cmd_is(cmd[0] , "del")) {
        *rescode = do_del(cmd, res, reslen);
    } else {
        *rescode = RES_ERR;
        const char *msg = "Unknown cmd";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }

    return 0;
}

static bool try_one_request(Conn *conn) {
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one request, generate the response.
    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(
        &conn->rbuf[4], len,
        &rescode, &conn->wbuf[4 + 4], &wlen
    );
    if (err) {
        conn->state = STATE_END;
        return false;
    }
    wlen += 4;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + wlen;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
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

static bool try_fill_buffer(Conn *conn) {
    // try to fill the buffer
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0) {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            msg("unexpected EOF");
        } else {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // Try to process requests one by one.
    // Why is there a loop? Please read the explanation of "pipelining".
    while (try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {}
}

static bool try_flush_buffer(Conn *conn) {
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
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}

static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {}
}

static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);  // not expected
    }
}