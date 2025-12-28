#!/usr/bin/env python3
import os

print("Status: 200 OK")
print("Content-Type: text/plain")
print("")

print("CWD:", os.getcwd())
try:
    with open("data.txt", "r") as f:
        content = f.read()
    print("data.txt content:")
    print(content)
except Exception as e:
    print("Error reading data.txt:", e)

