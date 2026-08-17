import socket
import time

s = socket.create_connection(("localhost", 8080), timeout=3)

# Send ONE request, without Connection: close, and WITHOUT sending a second
# request yet. If keep-alive works, the socket should stay open.
s.sendall(b"GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n")
s.settimeout(2)
resp1 = s.recv(4096)
print("--- Response 1 ---")
print(resp1.decode(errors="replace"))

# Wait a moment, then send a SECOND request on the SAME connection.
time.sleep(0.5)
s.sendall(b"GET /doesnotexist.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
resp2 = s.recv(4096)
print("--- Response 2 (same connection) ---")
print(resp2.decode(errors="replace"))

