#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "message.h"

int connected_total = 0;
int connected_now = 0;

static int bind_socket(int fd, int port)
{
    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    return bind(fd, (struct sockaddr*)&addr, sizeof(addr));
}

static int open_tcp_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);

    if (fd == -1)
    {
        perror("open_tcp_socket: socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt)) == -1)
    {
        perror("open_tcp_socket: setsockopt");
        exit(EXIT_FAILURE);
    }

    if (bind_socket(fd, port) == -1)
    {
        perror("open_tcp_socket: bind");
        exit(EXIT_FAILURE);
    }

    if (listen(fd, 128) == -1)
    {
        perror("open_tcp_socket: listen");
        exit(EXIT_FAILURE);
    }

    printf("tcp: listening on port %d\n", port);

    return fd;
}

static int open_udp_socket(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, 0);

    if (fd == -1)
    {
        perror("open_udp_socket");
        exit(EXIT_FAILURE);
    }

    if (bind_socket(fd, port) == -1)
    {
        perror("open_udp_socket: bind");
        exit(EXIT_FAILURE);
    }

    printf("udp: listening on port %d\n", port);

    return fd;
}

static void add_epoll_socket(int efd, int sfd, int events)
{
    struct epoll_event ev = {0};

    ev.events = events;
    ev.data.fd = sfd;

    if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) == -1)
    {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }
}

// Responds to client, and returns which type of response was sent
static response_type_t respond(int fd, char *buf, size_t bufsize)
{
    struct sockaddr_in addr = {0};
    socklen_t addrlen = 0;

    ssize_t length = recvfrom(fd, buf, bufsize, 0, (struct sockaddr*)&addr, &addrlen);

    if (length == -1)
    {
        perror("recvfrom");
        return RESPONSE_NONE;
    }

    buf[length] = 0;

    char *p;
    if ((p = strchr(buf, '\n')) != NULL)
        *p = 0; // Delet the \n

    printf("[from %s:%d] \"%s\"\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), buf);

    response_type_t response = parse_message(buf, length);

    const char *resp = NULL;
    size_t resplen = 0;

    switch (response)
    {
        case RESPONSE_UNKNOWN:
            resp = "Unknown command";
            resplen = strlen(resp)+1;
        break;

        case RESPONSE_MIRROR:
            resp = buf;
            resplen = length;
        break;

        case RESPONSE_TIME:
        {
            time_t curtime;
            time(&curtime);
            struct tm *t = localtime(&curtime);
            static char timestrbuf[64];

            snprintf(timestrbuf, sizeof(timestrbuf)-1, "%d-%d-%d %d:%d:%d", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

            resp = timestrbuf;
            resplen = strlen(resp)+1;
        }
        break;

        case RESPONSE_STATS:
        {
            char statstrbuf[64];
            snprintf(statstrbuf, sizeof(statstrbuf)-1, "Total connections: %d, currently connected: %d\n", connected_total, connected_now);

            resp = statstrbuf;
            resplen = strlen(resp)+1;
        }
        break;

        case RESPONSE_SHUTDOWN:
            resp = "Shutting down...";
            resplen = strlen(resp)+1;
        break;

        // Unreachable (parse_message never returns none), makes compiler shut up
        case RESPONSE_NONE:
        break;
    }

    if (sendto(fd, resp, resplen, 0, (struct sockaddr*)&addr, addrlen) < 0)
    {
        perror("respond: sendto");
    }

    return response;
}

int main()
{
    int running = 1;

    const int NUMEVENTS = 32;
    struct epoll_event events[NUMEVENTS];

    const int BUFSIZE = 1024;
    char *data = (char*)calloc(1, BUFSIZE+1);

    int efd = epoll_create1(0);

    if (efd == -1)
    {
        perror("epoll_create1");
        return EXIT_FAILURE;
    }

    int tcp_sock = open_tcp_socket(43210);
    int udp_sock = open_udp_socket(43211);

    add_epoll_socket(efd, tcp_sock, EPOLLIN);
    add_epoll_socket(efd, udp_sock, EPOLLIN);

    while (running)
    {
        int num_events = epoll_wait(efd, events, NUMEVENTS, -1);

        if (num_events == -1)
        {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < num_events; ++i)
        {
            if (events[i].data.fd == tcp_sock)
            {
                struct sockaddr_in addr = {0};
                socklen_t addrlen = 0;

                int conn_sock = accept(tcp_sock, (struct sockaddr*)&addr, &addrlen);

                if (conn_sock == -1)
                {
                    perror("accept");
                    continue;
                }

                printf("Connection from %s:%d\n", inet_ntoa(addr.sin_addr), addr.sin_port);

                fcntl(conn_sock, F_SETFL, fcntl(conn_sock, F_GETFL, 0)|O_NONBLOCK);

                add_epoll_socket(efd, conn_sock, EPOLLIN|EPOLLRDHUP);

                connected_now++;
                connected_total++;
            }
            else if (events[i].data.fd == udp_sock)
            {
                // Can't really keep track of connected udp clients... (since they can disconnect with no easy way of detecting that)
                // So just count total connections up at least :p
                connected_total++;

                if (respond(udp_sock, data, BUFSIZE) == RESPONSE_SHUTDOWN)
                    running = 0;
            }
            else if (events[i].events & EPOLLRDHUP)
            {
                connected_now--;

                if (epoll_ctl(efd, EPOLL_CTL_DEL, events[i].data.fd, NULL) == -1)
                {
                    perror("epoll_ctl");
                }

                if (close(events[i].data.fd) == -1)
                {
                    perror("close");
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                int conn_sock = events[i].data.fd;

                if (respond(conn_sock, data, BUFSIZE) == RESPONSE_SHUTDOWN)
                    running = 0;
            }
        }
    }

    free(data);

    close(udp_sock);
    close(tcp_sock);
    close(efd);

    return EXIT_SUCCESS;
}
