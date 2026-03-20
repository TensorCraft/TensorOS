#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "kmem_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  struct kmem_stats stats;
  kmem_init();

  uint32_t initial_free = kmem_free_bytes();
  uint32_t initial_largest = kmem_largest_free_block();
  void *a = kmem_alloc(64u);
  void *b = kmem_alloc(128u);
  struct kernel_event *event = kernel_event_create();
  void *oversized = kmem_alloc(8192u);

  expect(a != 0, "allocate first block");
  expect(b != 0, "allocate second block");
  expect(event != 0, "allocate event object");
  expect(oversized == 0, "oversized allocation fails");
  expect(kmem_free_bytes() < initial_free, "free bytes decrease");
  expect(kmem_largest_free_block() <= initial_largest, "largest free block does not grow");
  expect(kmem_bytes_in_use() >= 64u + 128u, "bytes in use tracks live allocations");
  expect(kmem_peak_bytes_in_use() >= kmem_bytes_in_use(), "peak bytes in use tracks high-water mark");
  expect(kmem_allocation_count() >= 3u, "allocation count tracks successful allocations");
  expect(kmem_allocation_fail_count() >= 1u, "allocation failure count tracks failures");
  expect(kmem_live_allocations() >= 3u, "live allocations tracks active objects");
  expect(kmem_peak_live_allocations() >= kmem_live_allocations(),
         "peak live allocations tracks high-water mark");
  expect(kernel_event_signal(event) == 3u, "event signal forwards wake count");
  expect(kernel_event_waiter_count(event) == 0u, "new event has no waiters");
  expect(kernel_event_destroy(event) == 1, "destroy event");
  expect(kmem_live_allocations() >= 2u, "destroy updates live allocation count");

  kmem_free(b);
  kmem_free(a);
  kmem_free(a);
  expect(kmem_free_bytes() >= initial_free - 32u, "free bytes mostly restored");
  expect(kmem_free_count() >= 3u, "free count tracks successful frees");
  expect(kmem_live_allocations() == 0u, "live allocations return to zero");

  kmem_stats_snapshot(&stats);
  expect(stats.arena_capacity_bytes >= initial_free, "snapshot reports arena capacity");
  expect(stats.free_bytes == kmem_free_bytes(), "snapshot free bytes matches direct query");
  expect(stats.largest_free_block == kmem_largest_free_block(),
         "snapshot largest free block matches direct query");
  expect(stats.bytes_in_use == kmem_bytes_in_use(), "snapshot bytes in use matches direct query");
  expect(stats.peak_bytes_in_use == kmem_peak_bytes_in_use(),
         "snapshot peak bytes matches direct query");
  expect(stats.allocation_count == kmem_allocation_count(),
         "snapshot allocation count matches direct query");
  expect(stats.free_count == kmem_free_count(), "snapshot free count matches direct query");
  expect(stats.allocation_fail_count == kmem_allocation_fail_count(),
         "snapshot fail count matches direct query");
  expect(stats.live_allocations == kmem_live_allocations(),
         "snapshot live allocations matches direct query");
  expect(stats.peak_live_allocations == kmem_peak_live_allocations(),
         "snapshot peak live allocations matches direct query");
  expect(stats.block_count >= 1u, "snapshot reports at least one block");
  expect(stats.free_block_count >= 1u, "snapshot reports a free block");
  expect(stats.used_block_count == 0u, "snapshot shows no used blocks after cleanup");

  puts("kmem host checks passed");
  return 0;
}
