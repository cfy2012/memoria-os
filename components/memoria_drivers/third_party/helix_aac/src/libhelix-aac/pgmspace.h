/*
 * pgmspace.h — ESP-IDF 平台适配层（非 Arduino 上游文件）
 *
 * 本头替代 Arduino 环境的 <pgmspace.h>：ESP32-S3 采用统一寻址空间，
 * PROGMEM 常量表无需 flash 读取指令，直接解引用即可。
 * 仅服务本目录下的 Helix AAC 第三方源码，不对外。
 */
#ifndef _PGMSPACE_H
#define _PGMSPACE_H

#define PROGMEM

#define pgm_read_byte(x)  (*(const unsigned char*)(x))
#define pgm_read_word(x)  (*(const unsigned short*)(x))

#endif /* _PGMSPACE_H */
