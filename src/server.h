#include "network.h"

#include <arpa/inet.h>  // net address transform
#include <netinet/ip.h> // ip data strcut
#include <sys/socket.h> // socket syscall
#include <sys/poll.h>

namespace H_redis {
    class Server {
        private :
            int fd;
            // 当前存在的连接（套接字），一个套接字就是一个端点
            // 分别对应着客户端的套接字和服务器的套接字
            std::vector<Conn *> fd2conn;

            // 轮询向量
            std::vector<struct pollfd> poll_args;

        public:
            int make_socket() {
                fd = socket(AF_INET, SOCK_STREAM, 0);
                if (fd < 0) {
                    die("socket()");
                }

                int val = 1;
                setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

                struct sockaddr_in addr = {};
                addr.sin_family = AF_INET;
                addr.sin_port = ntohs(1234);
                addr.sin_addr.s_addr = ntohl(0);

                // bind 
                // 在绑定相应的ip地址 + 端口号的时候，都需要将其转换成一个通用的套接字地址指针，即sockaddr
                // 除了sockaddr_in，还有sockaddr_in6，sockaddr_ll等类型
                int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));

                if(rv) {
                    die("bind()");
                }

                // listen
                // 设置一个最大监听的队列的长度
                rv = listen(fd, SOMAXCONN);

                fd_set_nb(fd);

                while(1) {
                    poll_args.clear();

                    struct pollfd pfd = {fd, POLLIN, 0};
                    poll_args.push_back(pfd);

                    for(Conn *conn : fd2conn) {
                        if(!conn) {
                            continue;
                        }

                        struct pollfd pfd = {};
                        pfd.fd = conn->fd;
                        pfd.events = (conn->state ? POLLOUT : POLLIN);

                        // 在事件上加上错误信息，便于调试
                        pfd.events |= POLLERR;
                        poll_args.push_back(pfd);
                    }

                    // 进行轮询活跃连接
                    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
                    if(rv < 0) {
                        die("poll()");
                    }

                    for (size_t i = 1; i < poll_args.size();i ++ ) {
                        if(poll_args[i].revents) {
                            Conn *conn = fd2conn[poll_args[i].fd];
                            connection_io(conn);

                            if(conn->state == STATE_END) {
                                // 连接结束
                                fd2conn[conn->fd] = NULL;
                                close(conn->fd);
                                free(conn);
                            }
                        }
                    }
                }

                 if (poll_args[0].revents) {
                    accept_new_conn(fd2conn, fd);
                }
            }
    };
}