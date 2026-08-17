#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>

// Everything that happens for ONE client now lives in its own function,
// so each thread can run this independently without stepping on other clients.
void handle_client(int client_fd) {
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf));
    std::cout << "Received " << n << " bytes\n";

    std::string response =
        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nHello World";
    write(client_fd, response.data(), response.size());

    close(client_fd);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));

    listen(listen_fd, 10);
    std::cout << "Listening on 8080...\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &len);

        // Spawn a new OS thread to handle this client, and immediately go
        // back to accept()-ing the next one. .detach() means we don't wait
        // for this thread to finish -- it runs independently in the background.
        std::thread(handle_client, client_fd).detach();
    }
    return 0;
}
