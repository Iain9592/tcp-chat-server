#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>

#define CLIENT_BUF_SIZE 8192
#define INPUT_LINE_MAX 4096
#define USERNAME_MAX 31

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

static int send_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off,
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
        off += (size_t)n;
    }

    return 0;
}

static int send_line(int fd, const char *line)
{
    if (line == NULL) {
        return -1;
    }

    return send_all(fd, line, strlen(line));
}

static int connect_to_server(const char *host, const char *port_str)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    int fd = -1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int flush_server_lines(char *buf, size_t *used)
{
    size_t i;
    size_t start = 0;

    for (i = 0; i < *used; i++) {
        if (buf[i] == '\n') {
            size_t linelen = i - start + 1;
            (void)fwrite(buf + start, 1, linelen, stdout);
            (void)fflush(stdout);
            start = i + 1;
        }
    }

    if (start > 0) {
        size_t remaining = *used - start;
        memmove(buf, buf + start, remaining);
        *used = remaining;
    }

    return 0;
}

static int wait_for_login_result(int server_fd, char *buf, size_t *used)
{
    for (;;) {
        size_t i;

        for (i = 0; i < *used; i++) {
            if (buf[i] == '\n') {
                char line[128];
                size_t linelen = i + 1;
                size_t copylen = linelen < sizeof(line) - 1 ? linelen : sizeof(line) - 1;

                memcpy(line, buf, copylen);
                line[copylen] = '\0';

                memmove(buf, buf + linelen, *used - linelen);
                *used -= linelen;

                if (strcmp(line, "OK\n") == 0) {
                    return 1;
                }
                if (strcmp(line, "ERROR\n") == 0) {
                    return 0;
                }

                (void)fwrite(line, 1, strlen(line), stdout);
                (void)fflush(stdout);
                break;
            }
        }

        if (*used == CLIENT_BUF_SIZE) {
            fprintf(stderr, "Client receive buffer full\n");
            return -1;
        }

        {
            ssize_t n = recv(server_fd, buf + *used, CLIENT_BUF_SIZE - *used, 0);
            if (n == 0) {
                fprintf(stderr, "Server disconnected\n");
                return -1;
            }
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recv");
                return -1;
            }
            *used += (size_t)n;
        }
    }
}

static int on_server_readable(int server_fd, char *buf, size_t *used)
{
    ssize_t n;

    if (*used == CLIENT_BUF_SIZE) {
        fprintf(stderr, "Client receive buffer full\n");
        return -1;
    }

    n = recv(server_fd, buf + *used, CLIENT_BUF_SIZE - *used, 0);
    if (n == 0) {
        if (*used > 0) {
            (void)fwrite(buf, 1, *used, stdout);
            (void)fflush(stdout);
            *used = 0;
        }
        fprintf(stderr, "Server disconnected\n");
        return -1;
    }
    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        perror("recv");
        return -1;
    }

    *used += (size_t)n;
    return flush_server_lines(buf, used);
}

static int prompt_and_login(int server_fd, char *server_buf, size_t *server_used)
{
    char username[USERNAME_MAX + 2];
    char line[USERNAME_MAX + 64];

    for (;;) {
        int lr;
        int n;

        fprintf(stdout, "Username: ");
        fflush(stdout);

        if (fgets(username, sizeof(username), stdin) == NULL) {
            return -1;
        }

        chomp_newline(username);

        if (username[0] == '\0') {
            fprintf(stderr, "Username cannot be empty\n");
            continue;
        }
        if (contains_space_or_tab(username)) {
            fprintf(stderr, "Username cannot contain spaces/tabs\n");
            continue;
        }

        n = snprintf(line, sizeof(line), "LOGIN %s\n", username);
        if (n < 0 || (size_t)n >= sizeof(line)) {
            fprintf(stderr, "Username too long\n");
            continue;
        }

        if (send_line(server_fd, line) < 0) {
            return -1;
        }

        lr = wait_for_login_result(server_fd, server_buf, server_used);
        if (lr < 0) {
            return -1;
        }
        if (lr == 1) {
            return 0;
        }

        fprintf(stderr, "Login failed\n");
    }
}

static int build_command(char *out, size_t out_size, const char *input)
{
    if (strcmp(input, "/quit") == 0 || strcmp(input, "QUIT") == 0) {
        return snprintf(out, out_size, "QUIT\n");
    }
    if (strncmp(input, "/join ", 6) == 0) {
        return snprintf(out, out_size, "JOIN %s\n", input + 6);
    }
    if (strcmp(input, "/who") == 0) {
        return snprintf(out, out_size, "WHO\n");
    }
    if (strncmp(input, "/pm ", 4) == 0) {
        return snprintf(out, out_size, "PM %s\n", input + 4);
    }

    return snprintf(out, out_size, "MSG %s\n", input);
}

static int client_event_loop(int server_fd)
{
    char server_buf[CLIENT_BUF_SIZE];
    size_t server_used = 0;
    char input[INPUT_LINE_MAX];
    char out[INPUT_LINE_MAX + 64];

    memset(server_buf, 0, sizeof(server_buf));

    fprintf(stdout, "Commands: /join <channel>, /who, /pm <user> <message>, /quit\n");
    fflush(stdout);

    for (;;) {
        fd_set read_fds;
        int max_fd;
        int ready;
        int n;

        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        max_fd = server_fd > STDIN_FILENO ? server_fd : STDIN_FILENO;
        ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            if (on_server_readable(server_fd, server_buf, &server_used) < 0) {
                return -1;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(input, sizeof(input), stdin) == NULL) {
                (void)send_line(server_fd, "QUIT\n");
                return 0;
            }

            chomp_newline(input);
            if (input[0] == '\0') {
                continue;
            }

            n = build_command(out, sizeof(out), input);
            if (n < 0 || (size_t)n >= sizeof(out)) {
                fprintf(stderr, "Input too long\n");
                continue;
            }

            if (send_line(server_fd, out) < 0) {
                perror("send");
                return -1;
            }

            if (strcmp(input, "/quit") == 0 || strcmp(input, "QUIT") == 0) {
                return 0;
            }
        }
    }
}

int main(int argc, char **argv)
{
    int fd;
    char server_buf[CLIENT_BUF_SIZE];
    size_t server_used = 0;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    fd = connect_to_server(argv[1], argv[2]);
    if (fd < 0) {
        fprintf(stderr, "Failed to connect to %s:%s\n", argv[1], argv[2]);
        return 1;
    }

    memset(server_buf, 0, sizeof(server_buf));
    if (prompt_and_login(fd, server_buf, &server_used) < 0) {
        close(fd);
        return 1;
    }

    (void)client_event_loop(fd);
    close(fd);
    return 0;
}
