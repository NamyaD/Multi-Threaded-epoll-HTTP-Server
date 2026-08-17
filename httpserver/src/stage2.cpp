#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr));

    listen(listen_fd, 10);

    std::cout << "Listening on 8080...\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd,
            (sockaddr*)&client_addr,
            &len
        );

        if (client_fd < 0) {
            std::cerr << "accept failed\n";
            continue;
        }

        std::cout << "Client connected\n";

        char buf[4096];

        ssize_t n = read(client_fd, buf, sizeof(buf));

        if (n > 0) {
            std::cout << "Received " << n << " bytes:\n";
            std::cout << std::string(buf, n) << "\n";
        }

        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "Hello World";

        write(client_fd, response.data(), response.size());

        close(client_fd);
    }

    return 0;
}
