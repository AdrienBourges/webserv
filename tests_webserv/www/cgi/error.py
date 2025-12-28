#!/usr/bin/env python3
print("Status: 200 OK")
print("Content-Type: text/plain")
print("")
raise RuntimeError("Intentional error in CGI")

