# Multi-Threaded epoll HTTP Server

A multi-threaded, event-driven HTTP/1.1 server built from scratch in C++ — raw POSIX sockets and Linux epoll, no frameworks. Built incrementally (see `src/stage1.cpp` through `src/stage5.cpp` for the build progression) up to the final multi-worker implementation in `src/server.cpp`.

## Architecture

- **Acceptor thread**: blocking `accept()` loop on the listening socket, hands off each new non-blocking connection round-robin to one of N worker threads.
- **Worker threads**: each owns its own `epoll` instance and runs an edge-triggered (`EPOLLET`) event loop — non-blocking I/O, no thread-per-connection.
- **HTTP parsing**: hand-written request-line + header parser (GET/HEAD), keep-alive support, and correct handling of multiple pipelined requests arriving in a single `read()` call.
- **Static file serving**: serves files from `www/`, with path-traversal protection (rejects any path containing `..`).
- **Graceful shutdown**: `SIGINT` cleanly stops the accept loop and all workers.

## Build & Run

```bash
g++ -std=c++17 -pthread src/server.cpp -o server
./server
```

Serves on port 8080, document root `www/`.

## Benchmark Results (measured against real nginx)

| Stage | Req/Sec | Notes |
|---|---|---|
| Thread-per-connection | 3,568 | Baseline; collapses under 2000 concurrent connections (18,717 socket errors) |
| Single-thread epoll | 12,361 | ~3.5x improvement from event-driven I/O |
| Multi-worker epoll (no keep-alive) | 3,568 | Regressed due to per-request TCP handshake overhead |
| Multi-worker epoll + keep-alive | 4,505 | Diagnosed the handshake bottleneck, fixed it, measured a 26% improvement |
| nginx (reference, same hardware) | 32,650 | Production-hardened: sendfile(), page caching, years of optimization |

Reproduce with:
```bash
wrk -t4 -c200 -d10s http://localhost:8080/
```

## Bugs Found and Fixed During Development

1. **Pipelined requests silently dropped**: with edge-triggered epoll (`EPOLLET`), a second HTTP request arriving in the same `read()` batch as the first would never trigger another epoll event (edge-triggered only notifies on *new* data). Fixed by looping over the read buffer and processing every complete request present, not just the first.

2. **Keep-alive handshake overhead**: forcing `Connection: close` on every response meant every one of 200 concurrent benchmark connections paid a full TCP handshake cost per request. Diagnosed by comparing socket error
