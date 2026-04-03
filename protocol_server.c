#include "protocol_server.h"

#include <stdio.h>
#include <string.h>

static void send_ok(int fd)
{
    (void)send_to_client(fd, "OK\n", 3);
}

static void send_error(int fd)
{
    (void)send_to_client(fd, "ERROR\n", 6);
}

static void chomp_newline(char *s)
{
    size_t n;

    if (s == NULL) {
        return;
    }

    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static char *skip_ws(char *s)
{
    while (s != NULL && (*s == ' ' || *s == '\t')) {
        s++;
    }
    return s;
}

static int contains_space_or_tab(const char *s)
{
    if (s == NULL) {
        return 0;
    }

    while (*s != '\0') {
        if (*s == ' ' || *s == '\t') {
            return 1;
        }
        s++;
    }

    return 0;
}

static int valid_name_token(const char *s, size_t max_len)
{
    size_t len;

    if (s == NULL || *s == '\0') {
        return 0;
    }
    if (contains_space_or_tab(s)) {
        return 0;
    }

    len = strlen(s);
    if (len == 0 || len >= max_len) {
        return 0;
    }

    return 1;
}

static int username_taken(const Client *clients, size_t max_clients, int exclude_index,
                          const char *username)
{
    size_t i;

    for (i = 0; i < max_clients; i++) {
        if ((int)i == exclude_index) {
            continue;
        }
        if (!clients[i].active || clients[i].fd < 0) {
            continue;
        }
        if (clients[i].username[0] == '\0') {
            continue;
        }
        if (strcmp(clients[i].username, username) == 0) {
            return 1;
        }
    }

    return 0;
}

static int find_client_by_username(const Client *clients, size_t max_clients,
                                   const char *username)
{
    size_t i;

    for (i = 0; i < max_clients; i++) {
        if (!clients[i].active || clients[i].fd < 0) {
            continue;
        }
        if (clients[i].username[0] == '\0') {
            continue;
        }
        if (strcmp(clients[i].username, username) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static void send_info_to_channel(const Client *clients, size_t max_clients,
                                 int exclude_index, const char *channel,
                                 const char *text)
{
    size_t i;

    if (clients == NULL || channel == NULL || text == NULL) {
        return;
    }

    for (i = 0; i < max_clients; i++) {
        if (!clients[i].active || clients[i].fd < 0) {
            continue;
        }
        if ((int)i == exclude_index) {
            continue;
        }
        if (clients[i].username[0] == '\0') {
            continue;
        }
        if (strcmp(clients[i].current_channel, channel) != 0) {
            continue;
        }

        (void)send_to_client(clients[i].fd, text, strlen(text));
    }
}

static void broadcast_to_channel(const Client *clients, size_t max_clients,
                                 int sender_index, const char *channel,
                                 const char *text)
{
    send_info_to_channel(clients, max_clients, sender_index, channel, text);
}

static void notify_channel_join(const Client *clients, size_t max_clients, int sender_index)
{
    char out[BUFFER_SIZE];
    int n;

    if (clients == NULL) {
        return;
    }
    if (sender_index < 0 || (size_t)sender_index >= max_clients) {
        return;
    }
    if (clients[sender_index].username[0] == '\0' ||
        clients[sender_index].current_channel[0] == '\0') {
        return;
    }

    n = snprintf(out, sizeof(out), "INFO %s joined channel %s\n",
                 clients[sender_index].username,
                 clients[sender_index].current_channel);
    if (n < 0 || (size_t)n >= sizeof(out)) {
        return;
    }

    send_info_to_channel(clients, max_clients, sender_index,
                         clients[sender_index].current_channel, out);
}

static void send_who_reply(const Client *clients, size_t max_clients, int sender_index)
{
    char out[BUFFER_SIZE];
    int written;
    size_t i;
    int first = 1;
    const char *channel = clients[sender_index].current_channel;

    if (channel[0] == '\0') {
        (void)send_to_client(clients[sender_index].fd,
                             "INFO Join a channel first with JOIN <channel>\n", 45);
        return;
    }

    written = snprintf(out, sizeof(out), "USERS %s:", channel);
    if (written < 0 || (size_t)written >= sizeof(out)) {
        send_error(clients[sender_index].fd);
        return;
    }

    for (i = 0; i < max_clients; i++) {
        int n;

        if (!clients[i].active || clients[i].fd < 0) {
            continue;
        }
        if (clients[i].username[0] == '\0') {
            continue;
        }
        if (strcmp(clients[i].current_channel, channel) != 0) {
            continue;
        }

        n = snprintf(out + (size_t)written, sizeof(out) - (size_t)written,
                     "%s%s", first ? " " : ", ", clients[i].username);
        if (n < 0 || (size_t)n >= sizeof(out) - (size_t)written) {
            break;
        }

        written += n;
        first = 0;
    }

    if ((size_t)written + 1 < sizeof(out)) {
        out[written] = '\n';
        out[written + 1] = '\0';
        (void)send_to_client(clients[sender_index].fd, out, strlen(out));
    } else {
        send_error(clients[sender_index].fd);
    }
}

static int handle_client_line(Client *clients, size_t max_clients, int sender_index, char *line)
{
    char *p;
    char *cmd;
    char *arg;

    p = skip_ws(line);
    if (p == NULL || *p == '\0') {
        send_error(clients[sender_index].fd);
        return 0;
    }

    cmd = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    if (*p != '\0') {
        *p = '\0';
        p++;
    }
    arg = skip_ws(p);

    if (strcmp(cmd, "QUIT") == 0) {
        return -1;
    }

    if (strcmp(cmd, "LOGIN") == 0) {
        size_t ulen;

        if (clients[sender_index].username[0] != '\0') {
            send_error(clients[sender_index].fd);
            return 0;
        }
        if (!valid_name_token(arg, USERNAME_LEN)) {
            send_error(clients[sender_index].fd);
            return 0;
        }
        if (username_taken(clients, max_clients, sender_index, arg)) {
            send_error(clients[sender_index].fd);
            return 0;
        }

        ulen = strlen(arg);
        memset(clients[sender_index].username, 0, sizeof(clients[sender_index].username));
        memcpy(clients[sender_index].username, arg, ulen);
        send_ok(clients[sender_index].fd);
        return 0;
    }

    if (clients[sender_index].username[0] == '\0') {
        send_error(clients[sender_index].fd);
        return 0;
    }

    if (strcmp(cmd, "JOIN") == 0) {
        size_t clen;
        char old_channel[CHANNEL_LEN];
        char out[BUFFER_SIZE];
        int n;

        if (!valid_name_token(arg, CHANNEL_LEN)) {
            send_error(clients[sender_index].fd);
            return 0;
        }

        memset(old_channel, 0, sizeof(old_channel));
        if (clients[sender_index].current_channel[0] != '\0') {
            memcpy(old_channel, clients[sender_index].current_channel,
                   sizeof(old_channel) - 1);
        }

        if (old_channel[0] != '\0') {
            n = snprintf(out, sizeof(out), "INFO %s left channel %s\n",
                   clients[sender_index].username, old_channel);
            if (n > 0 && (size_t)n < sizeof(out)) {
               send_info_to_channel(clients, max_clients, sender_index,
                             old_channel, out);
            }
        }
        clen = strlen(arg);
        memset(clients[sender_index].current_channel, 0,
               sizeof(clients[sender_index].current_channel));
        memcpy(clients[sender_index].current_channel, arg, clen);

        send_ok(clients[sender_index].fd);

        n = snprintf(out, sizeof(out), "INFO joined channel %s\n", arg);
        if (n > 0 && (size_t)n < sizeof(out)) {
            (void)send_to_client(clients[sender_index].fd, out, strlen(out));
        }

        notify_channel_join(clients, max_clients, sender_index);
        return 0;
    }

    if (strcmp(cmd, "WHO") == 0) {
        send_who_reply(clients, max_clients, sender_index);
        return 0;
    }

    if (strcmp(cmd, "PM") == 0) {
        char *target_name;
        char *text;
        int target_index;
        char out[BUFFER_SIZE];
        int n;

        if (arg == NULL || *arg == '\0') {
            send_error(clients[sender_index].fd);
            return 0;
        }

        target_name = arg;
        while (*arg != '\0' && *arg != ' ' && *arg != '\t') {
            arg++;
        }
        if (*arg == '\0') {
            send_error(clients[sender_index].fd);
            return 0;
        }

        *arg = '\0';
        arg++;
        text = skip_ws(arg);

        if (!valid_name_token(target_name, USERNAME_LEN) || text == NULL || *text == '\0') {
            send_error(clients[sender_index].fd);
            return 0;
        }

        target_index = find_client_by_username(clients, max_clients, target_name);
        if (target_index < 0 || target_index == sender_index) {
            send_error(clients[sender_index].fd);
            return 0;
        }

        n = snprintf(out, sizeof(out), "[PM from %s] %s\n",
                     clients[sender_index].username, text);
        if (n < 0 || (size_t)n >= sizeof(out)) {
            send_error(clients[sender_index].fd);
            return 0;
        }
        (void)send_to_client(clients[target_index].fd, out, strlen(out));

        n = snprintf(out, sizeof(out), "[PM to %s] %s\n", target_name, text);
        if (n > 0 && (size_t)n < sizeof(out)) {
            (void)send_to_client(clients[sender_index].fd, out, strlen(out));
        }
        return 0;
    }

    if (strcmp(cmd, "MSG") == 0) {
        char out[BUFFER_SIZE];
        int n;

        if (clients[sender_index].current_channel[0] == '\0') {
            (void)send_to_client(clients[sender_index].fd,
                                 "INFO Join a channel first with JOIN <channel>\n", 45);
            return 0;
        }
        if (arg == NULL || *arg == '\0') {
            send_error(clients[sender_index].fd);
            return 0;
        }

        n = snprintf(out, sizeof(out), "[%s] %s: %s\n",
                     clients[sender_index].current_channel,
                     clients[sender_index].username,
                     arg);
        if (n < 0 || (size_t)n >= sizeof(out)) {
            send_error(clients[sender_index].fd);
            return 0;
        }

        broadcast_to_channel(clients, max_clients, sender_index,
                             clients[sender_index].current_channel, out);
        return 0;
    }

    send_error(clients[sender_index].fd);
    return 0;
}

int protocol_on_bytes_received(Client *clients, size_t max_clients, int sender_index,
                               const char *bytes, ssize_t nbytes)
{
    Client *c;
    size_t i;
    size_t start;

    if (clients == NULL || bytes == NULL || nbytes <= 0) {
        return 0;
    }
    if (sender_index < 0 || (size_t)sender_index >= max_clients) {
        return 0;
    }

    c = &clients[sender_index];
    if (!c->active || c->fd < 0) {
        return 0;
    }

    if ((size_t)nbytes > sizeof(c->inbuf) - c->inbuf_used) {
        send_error(c->fd);
        return -1;
    }

    memcpy(c->inbuf + c->inbuf_used, bytes, (size_t)nbytes);
    c->inbuf_used += (size_t)nbytes;

    start = 0;
    for (i = 0; i < c->inbuf_used; i++) {
        if (c->inbuf[i] == '\n') {
            char line[BUFFER_SIZE + 1];
            size_t linelen;
            int rc;

            linelen = i - start + 1;
            if (linelen > BUFFER_SIZE) {
                send_error(c->fd);
                return -1;
            }

            memcpy(line, c->inbuf + start, linelen);
            line[linelen] = '\0';
            chomp_newline(line);

            rc = handle_client_line(clients, max_clients, sender_index, line);
            if (rc < 0) {
                return -1;
            }

            start = i + 1;
        }
    }

    if (start > 0) {
        size_t remaining = c->inbuf_used - start;
        memmove(c->inbuf, c->inbuf + start, remaining);
        c->inbuf_used = remaining;
        if (remaining == 0) {
            memset(c->inbuf, 0, sizeof(c->inbuf));
        }
    }

    return 0;
}
