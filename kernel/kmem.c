#include "runtime.h"

#include <stdint.h>

#define KMEM_ALIGNMENT 8u
#define KMEM_ARENA_SIZE 4096u

struct kmem_block {
  uint32_t size;
  uint32_t used;
  struct kmem_block *next;
};

static union {
  uint64_t align64;
  uint8_t bytes[KMEM_ARENA_SIZE];
} g_kmem_arena;

static struct kmem_block *g_kmem_head;
static uint8_t g_kmem_initialized;
static uint32_t g_kmem_bytes_in_use;
static uint32_t g_kmem_peak_bytes_in_use;
static uint32_t g_kmem_allocation_count;
static uint32_t g_kmem_free_count;
static uint32_t g_kmem_allocation_fail_count;
static uint32_t g_kmem_live_allocations;
static uint32_t g_kmem_peak_live_allocations;

static uint32_t kmem_align_up(uint32_t value) {
  return (value + (KMEM_ALIGNMENT - 1u)) & ~(KMEM_ALIGNMENT - 1u);
}

void kmem_init(void) {
  if (g_kmem_initialized != 0u) {
    return;
  }

  g_kmem_head = (struct kmem_block *)g_kmem_arena.bytes;
  g_kmem_head->size = KMEM_ARENA_SIZE - (uint32_t)sizeof(struct kmem_block);
  g_kmem_head->used = 0u;
  g_kmem_head->next = 0;
  g_kmem_bytes_in_use = 0u;
  g_kmem_peak_bytes_in_use = 0u;
  g_kmem_allocation_count = 0u;
  g_kmem_free_count = 0u;
  g_kmem_allocation_fail_count = 0u;
  g_kmem_live_allocations = 0u;
  g_kmem_peak_live_allocations = 0u;
  g_kmem_initialized = 1u;
}

static void kmem_split_block(struct kmem_block *block, uint32_t size) {
  uint32_t remaining = block->size - size;

  if (remaining <= (uint32_t)(sizeof(struct kmem_block) + KMEM_ALIGNMENT)) {
    return;
  }

  struct kmem_block *next =
      (struct kmem_block *)((uint8_t *)(block + 1) + size);
  next->size = remaining - (uint32_t)sizeof(struct kmem_block);
  next->used = 0u;
  next->next = block->next;

  block->size = size;
  block->next = next;
}

void *kmem_alloc(uint32_t size) {
  uint32_t aligned_size;

  kmem_init();

  if (size == 0u) {
    ++g_kmem_allocation_fail_count;
    return 0;
  }

  aligned_size = kmem_align_up(size);

  for (struct kmem_block *block = g_kmem_head; block != 0; block = block->next) {
    if ((block->used == 0u) && (block->size >= aligned_size)) {
      kmem_split_block(block, aligned_size);
      block->used = 1u;
      g_kmem_bytes_in_use += block->size;
      if (g_kmem_bytes_in_use > g_kmem_peak_bytes_in_use) {
        g_kmem_peak_bytes_in_use = g_kmem_bytes_in_use;
      }
      ++g_kmem_allocation_count;
      ++g_kmem_live_allocations;
      if (g_kmem_live_allocations > g_kmem_peak_live_allocations) {
        g_kmem_peak_live_allocations = g_kmem_live_allocations;
      }
      return (void *)(block + 1);
    }
  }

  ++g_kmem_allocation_fail_count;
  return 0;
}

static void kmem_coalesce(void) {
  for (struct kmem_block *block = g_kmem_head; block != 0; block = block->next) {
    while ((block->next != 0) && (block->used == 0u) && (block->next->used == 0u)) {
      block->size += (uint32_t)sizeof(struct kmem_block) + block->next->size;
      block->next = block->next->next;
    }
  }
}

void kmem_free(void *ptr) {
  if (ptr == 0) {
    return;
  }

  struct kmem_block *block = ((struct kmem_block *)ptr) - 1;
  if (block->used == 0u) {
    return;
  }

  block->used = 0u;
  if (g_kmem_bytes_in_use >= block->size) {
    g_kmem_bytes_in_use -= block->size;
  } else {
    g_kmem_bytes_in_use = 0u;
  }
  if (g_kmem_live_allocations != 0u) {
    --g_kmem_live_allocations;
  }
  ++g_kmem_free_count;
  kmem_coalesce();
}

uint32_t kmem_free_bytes(void) {
  kmem_init();

  uint32_t total = 0u;
  for (struct kmem_block *block = g_kmem_head; block != 0; block = block->next) {
    if (block->used == 0u) {
      total += block->size;
    }
  }
  return total;
}

uint32_t kmem_largest_free_block(void) {
  kmem_init();

  uint32_t largest = 0u;
  for (struct kmem_block *block = g_kmem_head; block != 0; block = block->next) {
    if ((block->used == 0u) && (block->size > largest)) {
      largest = block->size;
    }
  }
  return largest;
}

uint32_t kmem_bytes_in_use(void) {
  kmem_init();
  return g_kmem_bytes_in_use;
}

uint32_t kmem_peak_bytes_in_use(void) {
  kmem_init();
  return g_kmem_peak_bytes_in_use;
}

uint32_t kmem_allocation_count(void) {
  kmem_init();
  return g_kmem_allocation_count;
}

uint32_t kmem_free_count(void) {
  kmem_init();
  return g_kmem_free_count;
}

uint32_t kmem_allocation_fail_count(void) {
  kmem_init();
  return g_kmem_allocation_fail_count;
}

uint32_t kmem_live_allocations(void) {
  kmem_init();
  return g_kmem_live_allocations;
}

uint32_t kmem_peak_live_allocations(void) {
  kmem_init();
  return g_kmem_peak_live_allocations;
}

void kmem_stats_snapshot(struct kmem_stats *stats) {
  struct kmem_block *block;

  kmem_init();
  if (stats == 0) {
    return;
  }

  stats->arena_capacity_bytes = KMEM_ARENA_SIZE - (uint32_t)sizeof(struct kmem_block);
  stats->free_bytes = 0u;
  stats->largest_free_block = 0u;
  stats->bytes_in_use = g_kmem_bytes_in_use;
  stats->peak_bytes_in_use = g_kmem_peak_bytes_in_use;
  stats->allocation_count = g_kmem_allocation_count;
  stats->free_count = g_kmem_free_count;
  stats->allocation_fail_count = g_kmem_allocation_fail_count;
  stats->live_allocations = g_kmem_live_allocations;
  stats->peak_live_allocations = g_kmem_peak_live_allocations;
  stats->block_count = 0u;
  stats->free_block_count = 0u;
  stats->used_block_count = 0u;

  for (block = g_kmem_head; block != 0; block = block->next) {
    ++stats->block_count;
    if (block->used == 0u) {
      ++stats->free_block_count;
      stats->free_bytes += block->size;
      if (block->size > stats->largest_free_block) {
        stats->largest_free_block = block->size;
      }
    } else {
      ++stats->used_block_count;
    }
  }
}
