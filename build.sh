#!/bin/sh
clang -O1 -g -o test main.c -march=armv8+memtag -Wall