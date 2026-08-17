import socket

s = socket.create_connection(("localhost", 8080), timeout=2)

# Send TWO complete HTTP requests back-to-back in ONE sendall() call.
# On a real network this might arrive as one TCP read() on the server side,
# or it might not -- but sending them together like this maximizes the
# chance both land in the same read().
payload = (
    b"GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n"
    b"GET /doesnotexist.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"
)
s.sendall(payload)

s.settimeout(2)
data = b""
try:
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        data += chunk
except socket.timeout:
    pass

num_responses = data.count(b"HTTP/1.1")
print(f"Got {num_responses} response(s) back (expected 2)")
print("---")
print(data.decode(errors="replace"))
