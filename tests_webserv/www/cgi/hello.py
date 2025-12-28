#!/usr/bin/env python3
import os

print("")
print("Hello from CGI!")
print("REQUEST_METHOD=" + os.environ.get("REQUEST_METHOD", ""))

