#include "runtime_display.h"

#include <limits.h>

void runtime_display_service_init(struct runtime_display_service *service, uint16_t width,
                                  uint16_t height, uint32_t flags) {
  if (service == 0) {
    return;
  }

  service->width = width;
  service->height = height;
  service->flags = flags;
  service->present_count = 0u;
}

void runtime_display_service_mark_present(struct runtime_display_service *service) {
  if (service == 0) {
    return;
  }

  if (service->present_count != UINT32_MAX) {
    ++service->present_count;
  }
}

int32_t runtime_display_service_request(const struct runtime_display_service *service,
                                        uint32_t request, uint32_t *response) {
  if ((service == 0) || (response == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  switch (request) {
    case RUNTIME_DISPLAY_REQUEST_INFO:
      *response = runtime_display_pack_geometry(service->width, service->height);
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_DISPLAY_REQUEST_FLAGS:
      *response = service->flags;
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_DISPLAY_REQUEST_PRESENT_COUNT:
      *response = service->present_count;
      return RUNTIME_SYSCALL_STATUS_OK;
    default:
      return RUNTIME_SYSCALL_STATUS_ENOSYS;
  }
}
