#include <sys/socket.h>
#include <sys/epoll.h>
#include <sstream>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
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
                char buf[4096];
                ssize_t n = read(fd, buf, sizeof(buf));

                if (n <= 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                } else {
                    std::string request(buf, n);

                    // Request line looks like: "GET /index.html HTTP/1.1\r\n..."
                    // We only need the path, so pull out the second word.
                    size_t first_space = request.find(' ');
                    size_t second_space = request.find(' ', first_space + 1);
                    std::string method = request.substr(0, first_space);
                    std::string path = request.substr(first_space + 1, second_space - first_space - 1);

                    std::cout << "Method: " << method << ", Path: " << path << "\n";

                    // "/" should serve index.html by default
                    if (path == "/") path = "/index.html";

                    // SECURITY: reject any path containing ".." before touching
                    // the filesystem -- otherwise a request like
                    // "/../../etc/passwd" could read files outside www/.
                    std::string body;
                    int status = 200;
                    std::string status_text = "OK";

                    if (path.find("..") != std::string::npos) {
                        status = 400;
                        status_text = "Bad Request";
                        body = "400 Bad Request";
                    } else {
                        std::string full_path = "www" + path;
                        std::ifstream file(full_path, std::ios::binary);
                        if (!file) {
                            status = 404;
                            status_text = "Not Found";
                            body = "404 Not Found";
                        } else {
                            std::ostringstream ss;
                            ss << file.rdbuf();
                            body = ss.str();
                        }
                    }

                    std::string response =
                        "HTTP/1.1 " + std::to_string(status) + " " + status_text +
                        "\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body;
                    write(fd, response.data(), response.size());
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                }
            }
        }
    }
    return 0;
}
