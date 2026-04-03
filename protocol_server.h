#ifndef PROTOCOL_SERVER_H
#define PROTOCOL_SERVER_H

#include "server_utils.h"

#include <stddef.h>
#include <sys/types.h>

int protocol_on_bytes_received(Client *clients, size_t max_clients, int sender_index,
                               const char *bytes, ssize_t nbytes);

#endif
