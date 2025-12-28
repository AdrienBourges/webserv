#!/usr/bin/env python3
import sys

def main():
    # Lire TOUT le body depuis stdin jusqu'à EOF
    body = sys.stdin.read()

    # En-têtes CGI : une ligne "Content-Type:", puis une ligne vide
    sys.stdout.write("Content-Type: text/plain\r\n")
    sys.stdout.write("\r\n")

    # Corps de la réponse
    sys.stdout.write("You sent via POST:\n")
    sys.stdout.write(body)

if __name__ == "__main__":
    main()

