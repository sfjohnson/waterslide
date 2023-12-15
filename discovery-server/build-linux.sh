#!/bin/bash

clang -std=gnu17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra main.c -o waterslide-discovery-server-linux
