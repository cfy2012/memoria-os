#pragma once
/* GB2312 16x16 点阵字库接口
 * 点阵数据放 TF 卡 /font/gb2312_16.bin，启动时读入 PSRAM；
 * unicode 映射表编译在固件内（gb2312_font.cpp）。 */
#include <cstdint>

/* TF 卡点阵 bin 读入 PSRAM。成功 true；重复调用幂等。
 * 失败（缺卡/缺文件/尺寸不符）时汉字渲染降级为方框占位。 */
bool gb2312_font_load(const char* path);

/* 字库是否已就绪 */
bool gb2312_font_ready(void);

/* unicode -> GB2312 区位索引（区1-87全量），找不到返回 -1 */
int gb2312_index(uint32_t unicode);

/* 按索引取 32 字节点阵（每行 2 字节，高位在左）；未加载或越界返回 nullptr */
const uint8_t* gb2312_glyph(int gb_index);
