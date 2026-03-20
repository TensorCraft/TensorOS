#include <stdint.h>

#define ESP_APP_DESC_MAGIC_WORD 0xABCD5432u
#define MMU_PAGE_SIZE_LOG2      16u

typedef struct {
  uint32_t magic_word;
  uint32_t secure_version;
  uint32_t reserv1[2];
  char version[32];
  char project_name[32];
  char time[16];
  char date[16];
  char idf_ver[32];
  uint8_t app_elf_sha256[32];
  uint16_t min_efuse_blk_rev_full;
  uint16_t max_efuse_blk_rev_full;
  uint8_t mmu_page_size;
  uint8_t reserv3[3];
  uint32_t reserv2[18];
} esp_app_desc_t;

_Static_assert(sizeof(esp_app_desc_t) == 256, "esp_app_desc_t must be 256 bytes");

/*
 * The ESP-IDF 2nd stage bootloader expects segment #0 of an app image to begin
 * with an esp_app_desc_t in mapped DROM.
 */
const __attribute__((used)) __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .secure_version = 0,
    .version = "0.1.0",
    .project_name = "tensoros",
    .time = __TIME__,
    .date = __DATE__,
    .idf_ver = "bare-metal boot image",
    .min_efuse_blk_rev_full = 0,
    .max_efuse_blk_rev_full = 65535u,
    .mmu_page_size = MMU_PAGE_SIZE_LOG2,
};
