#include <inttypes.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

void bootloader_hooks_include(void) {
}

static uint32_t trace_reg_read(uint32_t reg)
{
    return REG_READ(reg);
}

void bootloader_after_init(void)
{
    esp_rom_delay_us(400000);
    ESP_LOGI("TRACE", "rtc_store0=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE0_REG));
    ESP_LOGI("TRACE", "rtc_store1=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE1_REG));
    ESP_LOGI("TRACE", "rtc_store2=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE2_REG));
    ESP_LOGI("TRACE", "rtc_store3=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE3_REG));
    ESP_LOGI("TRACE", "rtc_store4=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE4_REG));
    ESP_LOGI("TRACE", "rtc_store5=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE5_REG));
    ESP_LOGI("TRACE", "rtc_store6=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE6_REG));
    ESP_LOGI("TRACE", "rtc_store7=0x%08" PRIx32, trace_reg_read(RTC_CNTL_STORE7_REG));
}
