#include<stdio.h>
#include<time.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<sys/poll.h>
#include<sys/socket.h>
#include<errno.h>
#include<sys/time.h>
#include<netinet/in.h>
#include<pthread.h>
#include<string.h>
#include<unistd.h>
#include<sys/select.h>
#include<sys/socket.h>
#include"kv_store.h"


#define ENABLE_HTTP_RESPONSE 0

#define MAX_PORT 10



int accept_cb(int fd);
int recv_cb(int fd);
int send_cb(int fd);

struct conn_item connlist[BUFFER_LENGTH] = {0};
int epfd = 0;
struct timeval tv_begin;

#define TIME_SUB_MS(tv1, tv2) ((tv1.tv_sec - tv2.tv_sec)* 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

#if ENABLE_HTTP_RESPONSE

#define ROOT_DIR "/home/suyang/cpplinux/linux_server/netCode"

typedef struct conn_item connection_t;

int http_request(connection_t *conn) {
    return 0;
}

int http_response(connection_t *conn) {

    int filefd = open("index.html", O_RDONLY);

    struct stat stat_buf;
    fstat(filefd, &stat_buf);

    conn->wlen_ = sprintf(conn->wbuffer, "HTTP/1.1 200 OK\r\n"
                                         "Server: nginx\r\n"
                                         "Content-Type: text/html; charset=utf-8\r\n"
                                         "Content-Length: %lld\r\n"
                                         "Connection: keep-alive\r\n"
                                         "\r\n",
                          (long long)stat_buf.st_size);

    int count = read(filefd, conn->wbuffer + conn->wlen_, BUFFER_LENGTH - conn->wlen_);
    conn->wlen_ += count;

    close(filefd);
    return 0;
}


#endif

    int set_event(int fd, int event, int flag) {

        struct epoll_event ev;
    if (flag) {
        ev.events = event;
        ev.data.fd = fd;

        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    } else {
        ev.events = event;
        ev.data.fd = fd;

        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }

    return 1;
}

int accept_cb(int fd) {
    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);

    int clientfd = accept(fd, (struct sockaddr *)&clientaddr, &len);
    printf("accept clientfd: %d\n", clientfd);

    set_event(clientfd, EPOLLIN, 1);

    connlist[clientfd].fd = clientfd;

    memset(connlist[clientfd].rbuffer, 0, BUFFER_LENGTH);
    connlist[clientfd].rlen_ = 0;

    memset(connlist[clientfd].wbuffer, 0, BUFFER_LENGTH);
    connlist[clientfd].wlen_ = 0;

    connlist[clientfd].recv_t.recv_callback = recv_cb;
    connlist[clientfd].send_callback = send_cb;

    if (clientfd % 1000 == 999) {

        struct timeval tv_curr;
        gettimeofday(&tv_curr, NULL);
        int time_used = TIME_SUB_MS(tv_curr, tv_begin);

        memcpy(&tv_begin, &tv_curr, sizeof(struct timeval));

        printf("clientfd: %d, timeused %d\n", clientfd, time_used);
    }



    return 0;
}

int recv_cb(int fd) {
    char *buffer = connlist[fd].rbuffer;
    int idx = connlist[fd].rlen_;
    //int count = recv(fd, buffer + idx, BUFFER_LENGTH - idx, 0);

    int count = recv(fd, buffer, BUFFER_LENGTH, 0);
    if (count == 0)
    {
        printf("disconnect client: %d\n", fd);

        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);

        return 0;
    }

    idx += count;
    connlist[fd].rlen_= idx;

#if 0 //echo : to sendbuffer

    memcpy(connlist[fd].wbuffer, connlist[fd].rbuffer, connlist[fd].rlen_);

    connlist[fd].wlen_ = connlist[fd].rlen_;
    connlist[fd].rlen_ -= connlist[fd].rlen_;
#elif 0

    http_request(&connlist[fd]);
    http_response(&connlist[fd]);

#else

    kvstore_request(&connlist[fd]);
    connlist[fd].wlen_ = strlen(connlist[fd].wbuffer);
    //connlist[fd].rlen_ -= connlist[fd].rlen_;
    
#endif

    set_event(fd, EPOLLOUT, 0);

    return count;
}

int send_cb(int fd) {

    char *buffer = connlist[fd].wbuffer;
    int idx = connlist[fd].wlen_;

    int count = send(fd, buffer, idx, 0);

    set_event(fd, EPOLLIN, 0);

    return count;
}

int init_server(unsigned short port) {

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(port);

    if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
        perror("bind");
        return -1;
    }

    listen(sockfd, 10);
    return sockfd;
}

#if 0
int main()
{

    unsigned short port = 2048;
    epfd = epoll_create(1);

    for (int i = 0; i < MAX_PORT; ++i) {
        int sockfd = init_server(port + i);
        connlist[sockfd].fd = sockfd;
        connlist[sockfd].recv_t.accept_callback = accept_cb;
        set_event(sockfd, EPOLLIN, 1);

    }

    gettimeofday(&tv_begin, NULL);

    struct epoll_event events[1024] = {0};

    while (1) {
        int nready = epoll_wait(epfd, events, 1024, -1);

        int i = 0;
        for (i = 0; i < nready; ++i) {

            int connfd = events[i].data.fd;
            if (events[i].events & EPOLLIN)
            {
                int count = connlist[connfd].recv_t.recv_callback(connfd);
                if (count == 0)
                    continue;
                printf("recv <-- buffer: %s\n", connlist[connfd].rbuffer);
            }
            else if (events[i].events & EPOLLOUT)
            {
                int count = connlist[connfd].send_callback(connfd);
                printf("send --> buffer: %s\n", connlist[connfd].wbuffer);
            }
        }
    }

    getchar();
//    close(clientfd);
    return 0;
}
#else

int epoll_entry(void)
{

    unsigned short port = 2048;
    epfd = epoll_create(1);

    for (int i = 0; i < MAX_PORT; ++i) {
        int sockfd = init_server(port + i);
        connlist[sockfd].fd = sockfd;
        connlist[sockfd].recv_t.accept_callback = accept_cb;
        set_event(sockfd, EPOLLIN, 1);

    }

    gettimeofday(&tv_begin, NULL);

    struct epoll_event events[1024] = {0};

    while (1) {
        int nready = epoll_wait(epfd, events, 1024, -1);

        int i = 0;
        for (i = 0; i < nready; ++i) {

            int connfd = events[i].data.fd;
            if (events[i].events & EPOLLIN)
            {
                int count = connlist[connfd].recv_t.recv_callback(connfd);
                if (count == 0)
                    continue;
        //        printf("recv <-- buffer: %s\n", connlist[connfd].rbuffer);
            }
            else if (events[i].events & EPOLLOUT)
            {
                int count = connlist[connfd].send_callback(connfd);
         //       printf("send --> buffer: %s\n", connlist[connfd].wbuffer);
            }
        }
    }

    getchar();
//    close(clientfd);
    return 0;
}
#endif