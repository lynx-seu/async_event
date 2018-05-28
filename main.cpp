
#include "eventloop.h"
#include "anet.h"
#include <iostream>
#include <string>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>


int main()
{
    int ipfd = anetTcpServer(NULL, 8000, "0.0.0.0", 0);
    assert(ipfd != ANET_ERR);

    lynx::EventLoop el;

    auto write_f = [&el](int fd, lynx::IoMode mask, char *buffer) {


        el.DeleteFileEvent(fd, mask);
    };

    auto read_f = [&el, &write_f](int fd, lynx::IoMode mask) {
        int buffer_size = 1024;
        char *buffer = (char *)malloc(sizeof(char) * buffer_size);
        bzero(buffer, buffer_size);
        int size;
        size = read(fd, buffer, buffer_size);
        el.CreateIOEvent(fd, lynx::out, write_f, buffer);
    };

    auto accept_f = [&el, &read_f](int fd, int mask) {
        printf("fd = %d\n", fd);
        int client_port, client_fd;
        char client_ip[128];

        client_fd = anetTcpAccept(NULL, fd, client_ip, 128, &client_port);
        printf("Accepted %s:%d\n", client_ip, client_port);
        // set client socket non-block
        anetNonBlock(NULL, client_fd);
        // regist on message callback
        el.CreateIOEvent(client_fd, lynx::in, read_f);
    };
    bool ret = el.CreateIOEvent(ipfd, lynx::in, accept_f);
    assert(ret);

    el.Start();

	return 0;
}
