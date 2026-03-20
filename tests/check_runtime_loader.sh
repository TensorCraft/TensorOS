#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_loader_host_test.c kernel/runtime_loader.c \
  -o tests/runtime_loader_host_test

./tests/runtime_loader_host_test
