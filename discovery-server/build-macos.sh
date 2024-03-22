#!/bin/bash

clang -std=c17 -O3 -fstrict-aliasing -pedantic -pedantic-errors -Wall -Wextra main.c -o waterslide-ds-macos
