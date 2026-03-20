#pragma once

#include "runtime_loader.h"
#include "runtime_syscall.h"

#include <stddef.h>

#define RUNTIME_RESOURCE_PATH_PREFIX "/apps/"
#define RUNTIME_RESOURCE_PATH_RESOURCES "/resources/"

enum runtime_resource_request {
  RUNTIME_RESOURCE_REQUEST_PATH = 1u
};

struct runtime_resource_locator {
  const char *app_key;
  const char *resource_path;
};

int runtime_resource_validate_relative_path(const char *resource_path);
int32_t runtime_resource_build_path(const struct runtime_loader_record *records, uint32_t count,
                                    const struct runtime_resource_locator *locator, char *buffer,
                                    size_t capacity);
