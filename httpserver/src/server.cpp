#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <csignal>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <algorithm>

std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running = false;
}

// Per-connection state. Needed now because with keep-alive, a request might
// arrive split across multiple read() events, so we can't just process
// whatever we read and throw it away -- we accumulate into `buffer` until
// we have a complete request.
struct Connection {
    std::string buffer;
};

struct Worker {
    int epfd;
    std::thread thread;
    std::unordered_map<int, Connection> conns;
    std::mutex mtx; // guards conns, since add_connection() is called from the acceptor thread

    Worker() {
        epfd = epoll_create1(0);
    }

    void start() {
        thread = std::thread([this] { run(); });
    }

    void run() {
        epoll_event events[64];
        while (true) {
            int n = epoll_wait(epfd, events, 64, -1);
            for (int i = 0; i < n; i++) {
                int fd = events[i].data.fd;
                handle_readable(fd);
            }
        }
    }

    void handle_readable(int fd) {
        char buf[4096];
        ssize_t r = read(fd, buf, sizeof(buf));

        if (r <= 0) {
            close_connection(fd);
            return;
        }

        std::string* conn_buf;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = conns.find(fd);
            if (it == conns.end()) { close_connection(fd); return; }
            conn_buf = &it->second.buffer;
        }
        conn_buf->append(buf, r);

        std::string all_responses;
        bool should_close = false;

        // Process every complete request currently in the buffer.
        while (conn_buf->find("\r\n\r\n") != std::string::npos) {
            size_t header_end = conn_buf->find("\r\n\r\n");
            std::string one_request = conn_buf->substr(0, header_end);
            conn_buf->erase(0, header_end + 4);

            size_t first_space = one_request.find(' ');
            size_t second_space = one_request.find(' ', first_space + 1);
            std::string method = one_request.substr(0, first_space);
            std::string path = one_request.substr(first_space + 1, second_space - first_space - 1);

            // Determine keep-alive: default true for HTTP/1.1, false if the
            // client explicitly sent "Connection: close".
            bool keep_alive = true;
            std::string lower_req = one_request;
            std::transform(lower_req.begin(), lower_req.end(), lower_req.begin(), ::tolower);
            if (lower_req.find("connection: close") != std::string::npos) {
                keep_alive = false;
            }

            if (path == "/") path = "/index.html";

            std::string body;
            int status = 200;
            std::string status_text = "OK";

            if (path.find("..") != std::string::npos) {
                status = 400; status_text = "Bad Request"; body = "400 Bad Request";
            } else {
                std::string full_path = "www" + path;
                std::ifstream file(full_path, std::ios::binary);
                if (!file) {
                    status = 404; status_text = "Not Found"; body = "404 Not Found";
                } else {
                    std::ostringstream ss;
                    ss << file.rdbuf();
                    body = ss.str();
                }
            }

            std::cout << "[worker " << std::this_thread::get_id() << "] "
                      << method << " " << path << " -> " << status
                      << (keep_alive ? " (keep-alive)" : " (close)") << "\n";

            all_responses +=
                "HTTP/1.1 " + std::to_string(status) + " " + status_text +
                "\r\nContent-Length: " + std::to_string(body.size()) +
                "\r\nConnection: " + (keep_alive ? "keep-alive" : "close") +
                "\r\n\r\n" + body;

            if (!keep_alive) { should_close = true; break; }
        }

        if (!all_responses.empty()) {
            write(fd, all_responses.data(), all_responses.size());
        }

        if (should_close) {
            close_connection(fd);
        }
        // else: leave the connection open and registered in epoll -- the
        // NEXT request on this same socket will arrive as a future EPOLLIN
        // event, and we'll come right back into handle_readable().
    }

    void add_connection(int fd) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            conns.emplace(fd, Connection{});
        }
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    }

    void close_connection(int fd) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        std::lock_guard<std::mutex> lock(mtx);
        conns.erase(fd);
    }
};

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 1024);

    signal(SIGINT, handle_signal);

    std::cout << "Listening on 8080...\n";

    int num_workers = std::thread::hardware_concurrency();
    if (num_workers < 2) num_workers = 2;
    std::cout << "Starting " << num_workers << " worker threads\n";

    std::vector<std::unique_ptr<Worker>> workers;
    for (int i = 0; i < num_workers; i++) {
        workers.push_back(std::make_unique<Worker>());
        workers.back()->start();
    }

    size_t worker_index = 0;
    while (g_running) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (!g_running) break;
            continue;
        }

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        worker_index = (worker_index + 1) % workers.size();
        workers[worker_index]->add_connection(client_fd);
    }

    std::cout << "\nShutting down...\n";
    close(listen_fd);
    return 0;
}
