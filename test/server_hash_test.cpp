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

#include "../src/hashtable.h"
#include "../src/network.h"
#include "../src/server.h"
#include "../src/client.h"

int main() {
    H_redis::Server server{};
    server.make_socket();

    return 0;
}
