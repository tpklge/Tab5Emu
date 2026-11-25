// SPDX-License-Identifier: GPL-2.0-or-later
#include "probe.h"
#include "fixture_identity.h"
#include "paludo_platform.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "psa/crypto.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" int n64_probe_run(const uint8_t *,size_t,struct n64_probe_result *);
namespace {
void hex(const uint8_t *input,size_t n,char *output){constexpr char digits[]="0123456789abcdef";for(size_t i=0;i<n;++i){output[2*i]=digits[input[i]>>4];output[2*i+1]=digits[input[i]&15];}output[n*2]=0;}
int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
void send(const char *text){usb_serial_jtag_write_bytes(text,strlen(text),pdMS_TO_TICKS(1000));usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));}
}
extern "C" void app_main(){
 usb_serial_jtag_driver_config_t config{.tx_buffer_size=2048,.rx_buffer_size=512};
 if(!usb_serial_jtag_is_driver_installed())ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
 char elf[65];hex(esp_app_get_description()->app_elf_sha256,32,elf);
 char line[1024];esp_chip_info_t chip{};esp_chip_info(&chip);
 snprintf(line,sizeof(line),"TAB5N64_READY {\"firmware\":\"%s\",\"elf_sha256\":\"%s\",\"chip_revision\":%u,\"cpu_hz\":%u,\"fixture_sha256\":\"%s\"}\n",esp_app_get_description()->version,elf,unsigned(chip.revision),unsigned(esp_clk_cpu_freq()),N64_PROBE_SHA256);send(line);
 size_t used=0;const int64_t deadline=esp_timer_get_time()+30000000;
 while(esp_timer_get_time()<deadline && used<sizeof(line)-1){char ch;if(usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(100))!=1)continue;if(ch=='\n')break;if(ch!='\r')line[used++]=ch;}
 line[used]=0;uint8_t program[N64_PROBE_BYTES];bool valid=used==5+sizeof(program)*2 && !memcmp(line,"N64P ",5);
 if(valid)for(size_t i=0;i<sizeof(program);++i){const int hi=digit(line[5+i*2]),lo=digit(line[6+i*2]);if(hi<0 || lo<0){valid=false;break;}program[i]=uint8_t(hi*16+lo);}
 uint8_t digest[32];size_t digest_size=0;char sha[65]{};
 if(valid){valid=psa_crypto_init()==PSA_SUCCESS && psa_hash_compute(PSA_ALG_SHA_256,program,sizeof(program),digest,sizeof(digest),&digest_size)==PSA_SUCCESS && digest_size==32;if(valid){hex(digest,32,sha);valid=!strcmp(sha,N64_PROBE_SHA256);}}
 n64_probe_result result{};const int64_t start=esp_timer_get_time();
 const bool passed=valid && n64_probe_run(program,sizeof(program),&result);
 snprintf(line,sizeof(line),"TAB5N64_RESULT {\"passed\":%s,\"fixture_valid\":%s,\"instructions\":%u,\"core_bytes\":%u,\"cause\":%u,\"epc\":%u,\"elapsed_us\":%llu,\"unsupported\":\"%s\"}\n",passed?"true":"false",valid?"true":"false",unsigned(result.instructions),unsigned(result.core_bytes),unsigned(result.cause),unsigned(result.epc),(unsigned long long)(esp_timer_get_time()-start),result.unsupported?result.unsupported:"");send(line);
 vTaskDelay(pdMS_TO_TICKS(1500));
 if(!paludo_return_to_launcher()){send("TAB5N64_RETURN_UNAVAILABLE\n");for(;;)vTaskDelay(pdMS_TO_TICKS(1000));}
}
