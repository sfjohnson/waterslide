#!/bin/bash

g++ -std=c++17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -I./include/deps -L./lib main.cpp -o waterslide-monitor-udp-server -luwebsockets -pthread -lssl -lcrypto -lz
