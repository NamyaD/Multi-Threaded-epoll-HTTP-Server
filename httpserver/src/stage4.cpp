#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));

    listen(listen_fd, 1024); // bigger backlog now that we expect many connections

    // Make the LISTENING socket itself non-blocking too.
    fcntl(listen_fd, F_SETFL, O_NONBLOCK);

    // epoll_create1(): ask the kernel for a new "epoll instance" -- an object
    // that tracks a set of file descriptors and tells us which ones are ready.
    int epfd = epoll_create1(0);

    // Register the listening socket with epoll: "tell me when this fd has
    // data ready to read" (EPOLLIN = readable).
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    std::cout << "Listening on 8080 (epoll)...\n";

    epoll_event events[64];
    while (true) {
        // epoll_wait() BLOCKS here until at least one registered fd is ready.
        // Unlike accept()/read() blocking on ONE fd, this blocks while
        // watching potentially thousands of fds at once.
        int n = epoll_wait(epfd, events, 64, -1);

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                // The LISTENING socket is "readable" == a new client is waiting to be accepted.
                int client_fd = accept(listen_fd, nullptr, nullptr);
                fcntl(client_fd, F_SETFL, O_NONBLOCK);

                epoll_event cev{};
                cev.events = EPOLLIN;
                cev.data.fd = client_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev);
            } else {
                // An existing CLIENT socket is readable -- it has sent data.
                char buf[4096];
                ssize_t n = read(fd, buf, sizeof(buf));

                if (n <= 0) {
                    // 0 = client closed the connection. <0 = error.
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                } else {
                   std::string response =
                        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\nHello World"; 
                    write(fd, response.data(), response.size());
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
            }
        }
    }
    return 0;
}
