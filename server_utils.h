#ifndef SERVER_UTILS_H
#define SERVER_UTILS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stddef.h>

#define MAX_CLIENTS 64
#define BUFFER_SIZE 4096
#define USERNAME_LEN 32
#define CHANNEL_LEN 32
#define CLIENT_INBUF_SIZE 8192

typedef struct Client {
    int fd;
    int active;
    char username[USERNAME_LEN];
    char current_channel[CHANNEL_LEN];
    char inbuf[CLIENT_INBUF_SIZE];
    size_t inbuf_used;
} Client;

/* socket setup */
int create_listening_socket(int port);

/* client bookkeeping */
void init_clients(Client *clients, size_t max_clients);
int add_client(Client *clients, size_t max_clients, int client_fd);
void remove_client(Client *clients, size_t max_clients, int index);

/* select helpers */
int find_max_fd(int listen_fd, const Client *clients, size_t max_clients);
void build_fd_set(int listen_fd, const Client *clients, size_t max_clients, fd_set *read_fds);

/* send helpers */
int send_to_client(int client_fd, const void *buf, size_t len);

#endif
