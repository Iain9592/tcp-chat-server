#include "server_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

static void client_clear(Client *c)
{
    if (c == NULL) {
        return;
    }

    c->fd = -1;
    c->active = 0;
    memset(c->username, 0, sizeof(c->username));
    memset(c->current_channel, 0, sizeof(c->current_channel));
    memset(c->inbuf, 0, sizeof(c->inbuf));
    c->inbuf_used = 0;
}

int create_listening_socket(int port)
{
    int listen_fd = -1;
    int optval = 1;
    struct sockaddr_in addr;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return -1;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &optval, (socklen_t)sizeof(optval)) < 0) {
        close(listen_fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, (socklen_t)sizeof(addr)) < 0) {
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 16) < 0) {
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

void init_clients(Client *clients, size_t max_clients)
{
    size_t i;

    if (clients == NULL) {
        return;
    }

    for (i = 0; i < max_clients; i++) {
        client_clear(&clients[i]);
    }
}

int add_client(Client *clients, size_t max_clients, int client_fd)
{
    size_t i;

    if (clients == NULL || client_fd < 0) {
        return -1;
    }

    for (i = 0; i < max_clients; i++) {
        if (!clients[i].active) {
            clients[i].fd = client_fd;
            clients[i].active = 1;
            memset(clients[i].username, 0, sizeof(clients[i].username));
            memset(clients[i].current_channel, 0, sizeof(clients[i].current_channel));
            memset(clients[i].inbuf, 0, sizeof(clients[i].inbuf));
            clients[i].inbuf_used = 0;
            return (int)i;
        }
    }

    return -1;
}

void remove_client(Client *clients, size_t max_clients, int index)
{
    if (clients == NULL || index < 0 || (size_t)index >= max_clients) {
        return;
    }

    if (clients[index].active && clients[index].fd >= 0) {
        (void)close(clients[index].fd);
    }

    client_clear(&clients[index]);
}

int find_max_fd(int listen_fd, const Client *clients, size_t max_clients)
{
    size_t i;
    int max_fd = listen_fd;

    if (clients == NULL) {
        return max_fd;
    }

    for (i = 0; i < max_clients; i++) {
        if (clients[i].active && clients[i].fd > max_fd) {
            max_fd = clients[i].fd;
        }
    }

    return max_fd;
}

void build_fd_set(int listen_fd, const Client *clients, size_t max_clients, fd_set *read_fds)
{
    size_t i;

    if (read_fds == NULL) {
        return;
    }

    FD_ZERO(read_fds);
    if (listen_fd >= 0) {
        FD_SET(listen_fd, read_fds);
    }

    if (clients == NULL) {
        return;
    }

    for (i = 0; i < max_clients; i++) {
        if (clients[i].active && clients[i].fd >= 0) {
            FD_SET(clients[i].fd, read_fds);
        }
    }
}

int send_to_client(int client_fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t total = 0;

    if (client_fd < 0 || buf == NULL) {
        return -1;
    }

    while (total < len) {
        ssize_t n = send(client_fd, p + total, len - total,
#ifdef MSG_NOSIGNAL
                         MSG_NOSIGNAL
#else
                         0
#endif
        );
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        total += (size_t)n;
    }

    return 0;
}
