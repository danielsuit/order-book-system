#!/usr/bin/env python3
"""Send orders to any node via TCP."""
import socket
import sys

def send_command(host, port, command):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        s.sendall((command + "\n").encode())
        response = s.recv(4096).decode()
        print(f"[{host}:{port}] {response.strip()}")
        return response.strip()

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python send_order.py <host> <port> <command>")
        print('Example: python send_order.py localhost 8001 "ADD BUY 150.25 100"')
        sys.exit(1)
    host = sys.argv[1]
    port = int(sys.argv[2])
    command = sys.argv[3]
    send_command(host, port, command)
