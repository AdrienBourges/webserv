#!/usr/bin/env python3
import os
import sys

def main():
    # On essaye d'ouvrir data/sample.txt en relatif
    content = ""
    try:
        with open(os.path.join("data", "sample.txt"), "r", encoding="utf-8") as f:
            content = f.read()
    except Exception as e:
        content = f"ERROR: {repr(e)}"

    sys.stdout.write("Content-Type: text/html\r\n")
    sys.stdout.write("\r\n")
    sys.stdout.write("<!DOCTYPE html>\n")
    sys.stdout.write("<html><head><meta charset='utf-8'><title>CGI Relative Path</title></head><body>\n")
    sys.stdout.write("<h1>CGI Relative Path Test</h1>\n")
    sys.stdout.write("<pre>%s</pre>\n" % content)
    sys.stdout.write("</body></html>\n")

if __name__ == "__main__":
    main()

