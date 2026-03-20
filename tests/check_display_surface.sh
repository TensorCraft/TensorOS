#!/bin/sh
set -eu

cc -std=c11 -Wall -Wextra -Werror \
  -Iinclude \
  tests/display_surface_host_test.c kernel/display_surface.c \
  -o tests/display_surface_host_test

./tests/display_surface_host_test
