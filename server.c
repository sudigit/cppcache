#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
//You can replace read/write with send/recv. 
// The difference is that send/recv can pass some optional flags 
// that we don’t need.

// ssize_t read(int fd, void *buf, size_t len);
// ssize_t recv(int fd, void *buf, size_t len, int flags);         // read
// ssize_t write(int fd, const void *buf, size_t len);
// ssize_t send(int fd, const void *buf, size_t len, int flags);   // write

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void do_something(int connfd) {
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);  //last idx kep for \\'0'
    if (n < 0) {
        msg("read() error");
        return;
    }
    fprintf(stderr, "client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

int main(){
    // fd is file descriptor which are integers
    // AF_INET is for IPv4, AF_INET6 for IPv6 or dual-stack sockets.
    // SOCK_STREAM for TCP, OCK_DGRAM for UDP.
    // The 3rd argument is 0 and useless for our purposes.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    //3 arg is to specify which protocol 
    // but we are already doing it by 1st+2nd argu so no need hence 0

    // socket options like
    // TCP no delay, IP QoS, etc.
    //  These options are set via the setsockopt() API
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    //SOL_SOCKET = socket layer
    //SO_REUSEADDR the option 
    // val=1 means you enabling option and it requires the pointer to val


    //bind to the wildcard address 0.0.0.0:1234.
    //  This is just a parameter for listen().

    struct sockaddr_in addr = {};  //define your address
    addr.sin_family = AF_INET;     //ipv4
    // now it has something to do with big & little endian
    addr.sin_port = htons(1234);        // port  (Host to Network Short)
    addr.sin_addr.s_addr = htonl(0);    // wildcard IP 0.0.0.0   (Host to Network Long)
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) { die("bind()"); }  //rv=0 success, -1=error

     // listen
     // 2nd aargument is size of queue
     //SOMAXCONN is 4096 on Linux
    rv = listen(fd, SOMAXCONN);
    if (rv) { die("listen()"); }


    // loop that accepts and processes each client connection.
    while (true) {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        // now this is the socket for that particular client 
        // fd=listening socket, connfd=connected fd
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);

        if (connfd < 0) {
            continue;   // error
        }

        do_something(connfd);
        close(connfd);
    }

    return 0;
    
}