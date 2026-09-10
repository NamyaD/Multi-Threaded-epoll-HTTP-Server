#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

int main() {
    // socket(): ask the OS for a communication endpoint.
    // AF_INET = IPv4, SOCK_STREAM = TCP (reliable, ordered byte stream)
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);//socket create

    // Without this, restarting the server quickly gives "Address already in use"
    // because the OS holds the port in a TIME_WAIT state briefly after use.
    int opt = 1;// ON
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));//socket configure

    // bind(): assign this socket to a specific address + port.
    sockaddr_in addr{};//creates address
    addr.sin_family = AF_INET;//iPv4
    addr.sin_addr.s_addr = INADDR_ANY;//accept on any local interface
    addr.sin_port = htons(8080);
    bind(listen_fd, (sockaddr*)&addr, sizeof(addr));

    // listen(): start queuing incoming connection attempts. "10" = backlog size.
    listen(listen_fd, 10);
    std::cout << "Listening on 8080...\n";

    // accept(): BLOCKS until a client connects. Returns a NEW fd for that client;
    // listen_fd stays open to accept future connections.
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &len);
    std::cout << "Client connected\n";

    // read(): pull whatever bytes the client has sent so far.
    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf));
    std::cout << "Received " << n << " bytes:\n" << std::string(buf, n) << "\n";

    // write(): send a hardcoded, minimal valid HTTP response.
    std::string response =
        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nHello World";
    write(client_fd, response.data(), response.size());

    close(client_fd);
    close(listen_fd);
    return 0;
}
