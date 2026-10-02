/**
 * @file mp4_demux.hpp
 * @brief 轻量 MP4/M4A 容器解析：提取音频 sample 表
 *
 * 只解析播放所需的路径：
 *   ftyp → moov → trak(audio) → mdia → minf → stbl → stsd / stsz / stco / stsc
 * 输出：采样率 / 声道数 / 全部音频帧 [offset,size] 表。
 * 不做：视频轨、编辑列表、章节、元数据（不影响播放）。
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace memoria {
namespace drivers {

struct M4aSample {
    uint32_t offset;   /* 帧在文件中的绝对偏移 */
    uint32_t size;     /* 帧字节数（裸 AAC，无 ADTS 头） */
};

class M4aDemux {
public:
    M4aDemux() = default;
    ~M4aDemux() { close(); }
    M4aDemux(const M4aDemux&) = delete;
    M4aDemux& operator=(const M4aDemux&) = delete;

    /* 打开并解析容器。失败返回 false（错误原因见 error()） */
    bool open(const std::string& path);
    void close();

    /* 音频参数（解析成功后有值） */
    uint32_t sample_rate = 0;
    uint8_t  channels = 0;
    uint8_t  profile   = 2;   /* AudioObjectType：默认 AAC-LC */

    /* sample 表（close 后失效） */
    M4aSample* samples = nullptr;
    size_t     count = 0;

    /* 读取第 i 帧到 buf（最多 cap 字节），返回实际字节数；失败 0 */
    uint32_t read_sample(size_t i, uint8_t* buf, uint32_t cap) const;

    const char* error() const { return _err; }

private:    /* 表分配：优先 PSRAM，失败退回内部 RAM */
    bool _alloc_table(size_t n);
    bool _parse_moov(uint32_t box_off, uint32_t box_len);
    bool _parse_mdia(uint32_t off, uint32_t len);
    bool _parse_stbl(FILE* f, uint32_t off, uint32_t len);
    bool _parse_audio_entry(uint32_t off, uint32_t len);

    FILE*   _fp = nullptr;
    char    _err[64] = {};
};

/* 生成 7 字节 ADTS 头（把裸 AAC 帧包装成可解码的 ADTS 流） */
void m4a_make_adts(uint8_t* hdr, uint32_t sample_rate, uint8_t channels, uint32_t frame_len);

} // namespace drivers
} // namespace memoria
