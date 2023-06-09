#!/bin/bash

clang -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra -pthread main.c -o waterslide-discovery-server
