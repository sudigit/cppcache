// stdlib
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
// system
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
// C++
#include <string>
#include <vector>
#include <map>

#include "common.h"


const size_t k_max_args = 200 * 1000;

//make the fd non blocking
static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}


struct Conn {
    int fd = -1;
    //intensions of application,
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    // input output bufffer for application
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;

};

// Response::status
enum {
    RES_OK = 0,
    RES_ERR = 1,    // error
    RES_NX = 2,     // key not found
};

// +--------+---------+
// | status | data... |
// +--------+---------+
struct Response {
    uint32_t status = 0;
    std::vector<uint8_t> data;
};


static Conn *handle_accept(int fd){
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    // fd for the new connection socket client
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if(connfd<0){
        msg_errno("accept() error");
        return NULL;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    fd_set_nb(connfd); // make it non blocking

    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len){
    buf.insert(buf.end(), data, data+len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n){
    buf.erase(buf.begin(), buf.begin()+n);
}

static bool read_u32(const uint8_t *&curr, const uint8_t *end,  uint32_t &out){
    if(curr+4>end){
        return false;
    }
    memcpy(&out,curr, 4);
    curr+=4;
    return true;
}

static bool read_str(const uint8_t *&curr, const uint8_t *end, size_t n, std::string &out){
    if (curr + n > end) {
        return false;
    }
    out.assign(curr, curr + n);
    curr += n;
    return true;
}


// +------+-----+------+-----+------+-----+-----+------+
// | nstr | len | str1 | len | str2 | ... | len | strn |
// +------+-----+------+-----+------+-----+-----+------+

// eg request "get key1"
//it will look like
// len of request, len of token 1, token 1, len of token 2, token 2, ...

static int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out){
    const uint8_t *end = data + size;
    uint32_t nstr = 0;
    // nstr stores no of tokens in req
    if(!read_u32(data, end, nstr)){
        return -1;
    }
    if(nstr>k_max_args){
        return -1;
    }
    while(out.size()<nstr){
        uint32_t len=0;
        // read len of token
        if(!read_u32(data, end, len)){
            return -1;
        }
        out.push_back(std::string());
        // read token
        if(!read_str(data, end, len, out.back())){
            return -1;
        }
    }
    if(data!=end){
        return -1;
    }

    return 0;

}

// placeholder; implemented later
static std::map<std::string, std::string> g_data;


static void do_request(std::vector<std::string> &cmd, Response &out){
    // get key
    if(cmd.size()==2 && cmd[0]=="get"){
        auto it = g_data.find(cmd[1]);
        if(it==g_data.end()){
            out.status = RES_NX;
            return;
        }
        const std::string &val = it->second;
        out.data.assign(val.begin(), val.end());
    }
    // set key value
    else if(cmd.size()==3 && cmd[0]=="set"){
        g_data[cmd[1]]=cmd[2];
    }
    //swap key value
    else if(cmd.size()==3 && cmd[0]=="swap"){
        g_data[cmd[1]].swap(cmd[2]);
    }
    // del key
    else if(cmd.size()==2 && cmd[0]=="del"){
        g_data.erase(cmd[1]);
    }
    else{
        out.status = RES_ERR;
    }

}

static void make_response(const Response &resp, std::vector<uint8_t> &out){
    uint32_t resp_len = 4+(uint32_t)resp.data.size();
    buf_append(out, (const uint8_t *)&resp_len, 4);
    buf_append(out, (const uint8_t *)&resp.status, 4);
    buf_append(out, resp.data.data(), resp.data.size());
}


static bool try_one_request(Conn *conn){
    // check if enough data to represent length of request
    if(conn->incoming.size()<4){
        return false;
    }
    uint32_t len = 0;
    // get the len of request = first 4 bytes
    memcpy(&len, conn->incoming.data(), 4);
    if(len>k_max_msg){
        msg("too long");
        conn->want_close = true;
        return false;
    }
    // enough data of body of request
    if(conn->incoming.size()<4+len){
        return false; 
    }

    const uint8_t *request = &conn->incoming[4];

    std::vector<std::string> cmd;
    if(parse_req(request, len, cmd)<0){
        msg("bad request");
        conn->want_close = true;
        return false;
    }

    Response resp;
    do_request(cmd, resp);
    make_response(resp, conn->outgoing);

    // remove this request
    // we have more remaining 
    buf_consume(conn->incoming, 4+len);

    return true;

}

static void handle_write(Conn *conn){
    assert(conn->outgoing.size()>0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());

    if (rv < 0 && errno == EAGAIN) {
        return; // actually not ready
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;    // error handling
        return;
    }

    // remove written data from `outgoing`
    buf_consume(conn->outgoing, (size_t)rv);

    // update the readiness intention
    if (conn->outgoing.size() == 0) {   // all data written
        conn->want_read = true;
        conn->want_write = false;
    } // else: want write

}

static void handle_read(Conn *conn){
    uint8_t buf[64*1024];
    // no of bytes read, -1 means error, 0 means EOF
    ssize_t rv = read(conn->fd, buf, sizeof(buf)); 
    if(rv<0 && errno==EAGAIN){
        return;   //not ready to read
    }
    if(rv<0){
        msg_errno("read() error");
        conn->want_close = true;
        return;
    }
    if(rv==0){
        if(conn->incoming.size()==0){
            msg("client closed");
        }
        else{
            msg("unexpected EOF");
        }
        conn->want_close = true;
        return;
    }

    //read
    buf_append(conn->incoming, buf, rv);

    //pipeline of requests
    while(try_one_request(conn));

    if(conn->outgoing.size()>0){  // has response
        conn->want_read = false;
        conn->want_write = true;
        return handle_write(conn);
    }

}


int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    // fd is file descriptor, an int value to represent the socket
    if(fd<0){                       //socket creation failed
        die("socket()"); 
    }

    // val=1 means enable and val=0 means disable
    int val = 1;
    // set the socket to reuse the address
    // at socket level, let address be resuable, so that we can bind to the same address again after the server is closed
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // the address to bind to
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
    
    // now bind this addr
    int rv = bind(fd, (const sockaddr *) &addr, sizeof(addr));
    if (rv) {     // 0 success, -1 fail
        die("bind()");
    }

    fd_set_nb(fd);  // set the listen fd to nonblocking mode

    rv = listen(fd, SOMAXCONN);
    // second arg is the backlog, the max queue length for waiting
    // cat /proc/sys/net/core/somaxconn showed 4096
    if (rv) {      // 0 success, -1 fail
        die("listen()");
    }

    // all clients where idx = fd
    std::vector<Conn *> fd2conn;

    std::vector<struct pollfd> poll_args;

    while(true){
        poll_args.clear();
        // POLLIN means data to be read, POLLOUT means data to be written, POLLERR means error
        // 0 means ni events, this will be set by poll() to indicate what events happened
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for(Conn* conn: fd2conn){
            if(!conn){
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if(conn->want_read){
                pfd.events |= POLLIN;   
            }
            if(conn->want_write){
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
            // we are making a list of all the fds to poll,
            //  first the listening socket, then all the connection sockets
        }
        
        // wait for readiness for all pfds in poll_args, -1 means wait indefinitely
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR) {
            continue;   // not an error
        }
        if (rv < 0) {
            die("poll");
        }

        // handle the listening socket
        if (poll_args[0].revents) {
            if (Conn *conn = handle_accept(fd)) {
                // put it into the map
                if (fd2conn.size() <= (size_t)conn->fd) {
                    fd2conn.resize(conn->fd + 1);
                }
                assert(!fd2conn[conn->fd]);
                fd2conn[conn->fd] = conn;
                // put the fd in the list
            }
        }

        // now handle all the connection sockets
        for (size_t i = 1; i < poll_args.size(); ++i) {
            uint32_t ready = poll_args[i].revents;
            if (ready==0) {
                continue;
            }

            Conn *conn = fd2conn[poll_args[i].fd];
            
            if (ready & POLLIN) {
                assert(conn->want_read);
                handle_read(conn);
            }
            if (ready & POLLOUT) {
                assert(conn->want_write);
                handle_write(conn);
            }
            if ((ready & POLLERR) || conn->want_close) {
                (void)close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }
        }

    }

    return 0;
}