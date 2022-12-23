#!/bin/bash

/usr/local/opt/llvm/bin/clang -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -pthread main.c -o waterslide-discovery-server
