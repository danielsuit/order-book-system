#!/usr/bin/env python3
"""
Connect to all nodes, pull their book snapshots, and check if they match.
Run continuously to watch convergence happen in real time.
"""
import socket
import time

NODES = [
    ("localhost", 8001),
    ("localhost", 8002),
    ("localhost", 8003),
    ("localhost", 8004),
    ("localhost", 8005),
]

def get_book(host, port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(2)
        s.connect((host, port))
        s.sendall(b"BOOK 5\n")
        return s.recv(8192).decode().strip()

while True:
    books = []
    for host, port in NODES:
        try:
            book = get_book(host, port)
            books.append(book)
            print(f"  [{host}:{port}] {book[:80]}...")
        except Exception as e:
            print(f"  [{host}:{port}] UNREACHABLE: {e}")
            books.append(None)

    alive_books = [b for b in books if b is not None]
    if len(alive_books) > 1 and len(set(alive_books)) == 1:
        print(">>> ALL NODES CONVERGED <<<")
    else:
        print(f">>> {len(set(alive_books))} distinct states across {len(alive_books)} nodes <<<")

    print()
    time.sleep(1)
