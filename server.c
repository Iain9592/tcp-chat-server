#include <string.h>
#include "server_utils.h"
#include "protocol_server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>

static int parse_port(const char *s, int *out_port)
{
    char *end = NULL;
    long val;

    if (s == NULL || out_port == NULL) {
        return -1;
    }

    errno = 0;
    val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    if (val < 1 || val > 65535) {
        return -1;
    }

    *out_port = (int)val;
    return 0;
}

static void notify_disconnect(Client *clients, size_t max_clients, int leaving_index)
{
    char out[BUFFER_SIZE];
    int n;
    size_t i;

    if (clients[leaving_index].username[0] == '\0' ||
        clients[leaving_index].current_channel[0] == '\0') {
        return;
    }

    n = snprintf(out, sizeof(out), "INFO %s left channel %s\n",
                 clients[leaving_index].username,
                 clients[leaving_index].current_channel);
    if (n < 0 || (size_t)n >= sizeof(out)) {
        return;
    }

    for (i = 0; i < max_clients; i++) {
        if ((int)i == leaving_index) {
            continue;
        }
        if (!clients[i].active || clients[i].fd < 0) {
            continue;
        }
        if (clients[i].username[0] == '\0') {
            continue;
        }
        if (strcmp(clients[i].current_channel,
                   clients[leaving_index].current_channel) != 0) {
            continue;
        }

        (void)send_to_client(clients[i].fd, out, strlen(out));
    }
}

int main(int argc, char **argv)
{
    int port;
    int listen_fd = -1;
    Client clients[MAX_CLIENTS];
    char buffer[BUFFER_SIZE];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    if (parse_port(argv[1], &port) < 0) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    listen_fd = create_listening_socket(port);
    if (listen_fd < 0) {
        perror("create_listening_socket");
        return 1;
    }

    init_clients(clients, MAX_CLIENTS);

    for (;;) {
        fd_set read_fds;
        int max_fd;
        int ready;
        size_t i;

        build_fd_set(listen_fd, clients, MAX_CLIENTS, &read_fds);
        max_fd = find_max_fd(listen_fd, clients, MAX_CLIENTS);

        ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_storage peer_addr;
            socklen_t peer_len = (socklen_t)sizeof(peer_addr);
            int client_fd;
            int idx;

            client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_len);
            if (client_fd < 0) {
                perror("accept");
            } else {
                idx = add_client(clients, MAX_CLIENTS, client_fd);
                if (idx < 0) {
                    (void)send_to_client(client_fd, "Server full\n", 12);
                    (void)close(client_fd);
                }
            }
        }

        for (i = 0; i < MAX_CLIENTS; i++) {
            ssize_t n;

            if (!clients[i].active || clients[i].fd < 0) {
                continue;
            }
            if (!FD_ISSET(clients[i].fd, &read_fds)) {
                continue;
            }

            n = recv(clients[i].fd, buffer, sizeof(buffer), 0);
            if (n == 0) {
                notify_disconnect(clients, MAX_CLIENTS, (int)i);
                remove_client(clients, MAX_CLIENTS, (int)i);
                continue;
            }
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                notify_disconnect(clients, MAX_CLIENTS, (int)i);
                perror("recv");
                remove_client(clients, MAX_CLIENTS, (int)i);
                continue;
            }

            if (protocol_on_bytes_received(clients, MAX_CLIENTS, (int)i, buffer, n) < 0) {
                notify_disconnect(clients, MAX_CLIENTS, (int)i);
                remove_client(clients, MAX_CLIENTS, (int)i);
            }
        }
    }

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            remove_client(clients, MAX_CLIENTS, (int)i);
        }
    }
    if (listen_fd >= 0) {
        (void)close(listen_fd);
    }

    return 1;
}
