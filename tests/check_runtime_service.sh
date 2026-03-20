#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/runtime_service_host_test.c kernel/runtime_service.c \
  -o tests/runtime_service_host_test

./tests/runtime_service_host_test
