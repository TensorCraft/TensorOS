#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_resource_host_test.c kernel/runtime_resource.c kernel/runtime_loader.c \
  -o tests/runtime_resource_host_test

./tests/runtime_resource_host_test
