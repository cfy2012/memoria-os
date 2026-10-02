/**
 * @file mp4_demux.cpp
 * @brief 轻量 MP4/M4A 容器解析（实现）
 *
 * box 结构： [size:32][type:32][payload]；size==1 时后接 64 位扩展长度。
 * 只关心 moov 里的音频路径，其余 box 全部跳过。
 */

#include "mp4_demux.hpp"

extern "C" {
#include <esp_heap_caps.h>
}

#include <cstring>

namespace memoria {
namespace drivers {

/* ---------- 大端读取辅助 ---------- */
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) << 32 | rd32(p + 4); }

/* 临时解析表分配：优先 PSRAM，失败回退内部 RAM（同 _alloc_table 策略）
 * 长曲目三表（stsz/stco/stsc）合计可达数十 KB，不应挤占内部 SRAM */
static void* tbl_malloc(size_t bytes) {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(bytes);
}

/* 四字符 box type：单字符常量拼接，避免 multichar 告警 */
#define FCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* 采样率 → ADTS 频率索引 */
static uint8_t adts_freq_index(uint32_t sr) {
    static const uint32_t tbl[] = {96000, 88200, 64000, 48000, 44100, 32000,
                                   24000, 22050, 16000, 12000, 11025, 8000};
    for (int i = 0; i < 12; i++)
        if (sr == tbl[i]) return (uint8_t)i;
    return 4; /* 未知时按 44100 */
}

bool M4aDemux::_alloc_table(size_t n) {
    if (n == 0) return false;
    samples = (M4aSample*)heap_caps_malloc(n * sizeof(M4aSample),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!samples)
        samples = (M4aSample*)malloc(n * sizeof(M4aSample));   /* 内部 RAM 回退 */
    if (!samples) { snprintf(_err, sizeof(_err), "sample table oom"); return false; }
    count = n;
    return true;
}

bool M4aDemux::open(const std::string& path) {
    close();
    _fp = fopen(path.c_str(), "rb");
    if (!_fp) { snprintf(_err, sizeof(_err), "open fail"); return false; }

    /* 顶层 box 扫描：moov 可能在文件任意位置（含 mdat 之后） */
    uint8_t hdr[16];
    uint64_t off = 0;
    fseeko(_fp, 0, SEEK_END);
    uint64_t fsize = ftello(_fp);

    while (off + 8 <= fsize) {
        if (fseeko(_fp, (off_t)off, SEEK_SET) != 0 || fread(hdr, 1, 8, _fp) != 8) break;
        uint32_t size = rd32(hdr);
        uint32_t type = rd32(hdr + 4);
        uint64_t hdrlen = 8;
        if (size == 1) {                                   /* 64 位扩展长度 */
            if (fread(hdr + 8, 1, 8, _fp) != 8) break;
            uint64_t esize = rd64(hdr + 8);
            if (esize < 16) break;
            off += esize;
            continue;
        }
        if (size == 0) break;                              /* 到文件尾 */
        if (type == FCC('m', 'o', 'o', 'v')) {
            if (!_parse_moov((uint32_t)(off + hdrlen), (uint32_t)(size - hdrlen))) return false;
            return true;                                    /* 只解析一个 moov */
        }
        off += size;
    }
    snprintf(_err, sizeof(_err), "no moov box");
    return false;
}

void M4aDemux::close() {
    if (_fp) { fclose(_fp); _fp = nullptr; }
    if (samples) { free(samples); samples = nullptr; }
    count = 0;
    sample_rate = 0;
    channels = 0;
}

bool M4aDemux::_parse_moov(uint32_t box_off, uint32_t box_len) {
    /* 递归找 audio trak：moov → trak → mdia → minf → stbl */
    uint8_t hdr[16];
    uint64_t p = box_off, end = box_off + box_len;
    while (p + 8 <= end) {
        if (fseeko(_fp, (off_t)p, SEEK_SET) != 0 || fread(hdr, 1, 8, _fp) != 8) return false;
        uint32_t size = rd32(hdr), type = rd32(hdr + 4);
        uint64_t hdrlen = 8;
        if (size == 1) {
            if (fread(hdr + 8, 1, 8, _fp) != 8) return false;
            size = (uint32_t)rd64(hdr + 8);
            hdrlen = 16;
        }
        if (size < hdrlen) return false;

        if (type == FCC('t', 'r', 'a', 'k')) {
            /* 只取音频轨 */
            if (fseeko(_fp, (off_t)(p + hdrlen), SEEK_SET) != 0) return false;
            /* 跳过 tkhd，直接找 mdia */
            uint64_t q = p + hdrlen, qend = p + size;
            while (q + 8 <= qend) {
                if (fseeko(_fp, (off_t)q, SEEK_SET) != 0 || fread(hdr, 1, 8, _fp) != 8) return false;
                uint32_t sz = rd32(hdr), ty = rd32(hdr + 4);
                uint64_t hl = 8;
                if (sz == 1) {
                    if (fread(hdr + 8, 1, 8, _fp) != 8) return false;
                    sz = (uint32_t)rd64(hdr + 8);
                    hl = 16;
                }
                if (sz < hl) return false;
                if (ty == FCC('m', 'd', 'i', 'a')) {
                    if (!_parse_mdia(q + hl, sz - (uint32_t)hl)) return false;
                    return true;   /* 音频轨解析完成 */
                }
                q += sz;
            }
        }
        p += size;
    }
    snprintf(_err, sizeof(_err), "no audio trak");
    return false;
}

bool M4aDemux::_parse_mdia(uint32_t off, uint32_t len) {
    uint8_t hdr[16];
    uint64_t p = off, end = off + len;
    while (p + 8 <= end) {
        if (fseeko(_fp, (off_t)p, SEEK_SET) != 0 || fread(hdr, 1, 8, _fp) != 8) return false;
        uint32_t size = rd32(hdr), type = rd32(hdr + 4);
        uint64_t hl = 8;
        if (size == 1) {
            if (fread(hdr + 8, 1, 8, _fp) != 8) return false;
            size = (uint32_t)rd64(hdr + 8);
            hl = 16;
        }
        if (size < hl) return false;
        if (type == FCC('m', 'i', 'n', 'f')) {
            /* minf → stbl（跳过 hdlr/dinf） */
            uint64_t q = p + hl, qend = p + size;
            while (q + 8 <= qend) {
                if (fseeko(_fp, (off_t)q, SEEK_SET) != 0 || fread(hdr, 1, 8, _fp) != 8) return false;
                uint32_t sz = rd32(hdr), ty = rd32(hdr + 4);
                uint64_t h2 = 8;
                if (sz == 1) {
                    if (fread(hdr + 8, 1, 8, _fp) != 8) return false;
                    sz = (uint32_t)rd64(hdr + 8);
                    h2 = 16;
                }
                if (sz < h2) return false;
                if (ty == FCC('s', 't', 'b', 'l')) {
                    if (!_parse_stbl(_fp, (uint32_t)(q + h2), sz - (uint32_t)h2)) return false;
                    return true;
                }
                q += sz;
            }
        }
        p += size;
    }
    snprintf(_err, sizeof(_err), "no minf/stbl");
    return false;
}

bool M4aDemux::_parse_stbl(FILE* f, uint32_t off, uint32_t len) {
    uint8_t hdr[16];
    uint64_t p = off, end = off + len;
    uint32_t sample_count = 0;
    uint32_t* chunk_offs = nullptr;    /* stco 的 chunk 偏移表 */
    uint32_t n_co = 0;                 /* stco 条目数 */
    uint32_t* sample_szs = nullptr;    /* stsz 的逐帧大小表（非 uniform 时） */
    uint32_t uniform_sz = 0;
    uint32_t* sc = nullptr;            /* stsc：entry 数组 {first_chunk, samples_per_chunk, desc} */
    uint32_t sc_entries = 0;
    bool ok = false;

    while (p + 8 <= end) {
        if (fseeko(f, (off_t)p, SEEK_SET) != 0 || fread(hdr, 1, 8, f) != 8) break;
        uint32_t size = rd32(hdr), type = rd32(hdr + 4);
        uint64_t hl = 8;
        if (size == 1) {
            if (fread(hdr + 8, 1, 8, f) != 8) break;
            size = (uint32_t)rd64(hdr + 8);
            hl = 16;
        }
        if (size < hl) break;
        uint32_t plen = size - (uint32_t)hl;

        if (type == FCC('s', 't', 's', 'd')) {
            /* stsd: [version/flags:4][entry_count:4][entry...] 取首个 mp4a */
            if (!_parse_audio_entry((uint32_t)(p + hl + 8), plen - 8)) goto done;
        } else if (type == FCC('s', 't', 's', 'z')) {
            /* stsz: [vf:4][sample_size:4][count:4][sizes...] */
            uint8_t b[12];
            if (fseeko(f, (off_t)(p + hl), SEEK_SET) != 0 || fread(b, 1, 12, f) != 12) break;
            uniform_sz = rd32(b + 4);
            sample_count = rd32(b + 8);
            if (uniform_sz == 0) {
                if (plen < 12 + (uint64_t)sample_count * 4) break;
                sample_szs = (uint32_t*)tbl_malloc(sample_count * 4);
                if (!sample_szs) goto done;
                if (fread(sample_szs, 1, sample_count * 4, f) != sample_count * 4) goto done;
            }
        } else if (type == FCC('s', 't', 'c', 'o')) {
            /* stco: [vf:4][count:4][offsets...] */
            uint8_t b[8];
            if (fseeko(f, (off_t)(p + hl), SEEK_SET) != 0 || fread(b, 1, 8, f) != 8) break;
            uint32_t n = rd32(b + 4);
            if (plen < 8 + (uint64_t)n * 4) break;
            chunk_offs = (uint32_t*)tbl_malloc(n * 4);
            if (!chunk_offs) goto done;
            if (fread(chunk_offs, 1, n * 4, f) != n * 4) goto done;
            n_co = n;
        } else if (type == FCC('s', 't', 's', 'c')) {
            /* stsc: [vf:4][count:4][entries: first_chunk, samples_per_chunk, desc] */
            uint8_t b[8];
            if (fseeko(f, (off_t)(p + hl), SEEK_SET) != 0 || fread(b, 1, 8, f) != 8) break;
            sc_entries = rd32(b + 4);
            if (plen < 8 + (uint64_t)sc_entries * 12) break;
            sc = (uint32_t*)tbl_malloc(sc_entries * 12);
            if (!sc) goto done;
            if (fread(sc, 1, sc_entries * 12, f) != sc_entries * 12) goto done;
        }
        p += size;
    }

    /* 重建 sample 表：按 chunk 顺序展开 */
    if (sample_count && chunk_offs && sc_entries && _alloc_table(sample_count)) {
        size_t n = 0;
        uint32_t per_chunk = 1;
        uint32_t sci = 0;
        uint32_t chunk_idx = 1;
        /* stco 的条目数即 chunk 数（按 stsc 前推） */
        for (chunk_idx = 1; ; chunk_idx++) {
            if (chunk_idx - 1 >= n_co) break;           /* 防 stsc 隐含 chunk 数超过 stco 条目数 */
            /* 当前 chunk 的 sample 数：应用 stsc 规则 */
            while (sci + 1 < sc_entries && sc[(sci + 1) * 3] <= chunk_idx) sci++;
            per_chunk = sc[sci * 3 + 1];
            uint32_t off = chunk_offs[chunk_idx - 1];
            for (uint32_t s = 0; s < per_chunk && n < sample_count; s++, n++) {
                uint32_t sz = uniform_sz ? uniform_sz : sample_szs[n];
                samples[n] = {off, sz};
                off += sz;
            }
            if (n >= sample_count) break;
            if (chunk_idx >= 4096 && n < sample_count) {   /* 防御：stco 条数异常 */
                /* 理论上 chunk 数=stco 条目数；此处按实际表长截断 */
                break;
            }
        }
        /* stco 条目数才是权威 chunk 数；若上面循环提前结束（uniform 表满），正常 */
        ok = (n == sample_count);
        if (!ok) snprintf(_err, sizeof(_err), "sample table mismatch");
    } else {
        snprintf(_err, sizeof(_err), "missing stsz/stco/stsc");
    }

done:
    if (!ok && samples) { free(samples); samples = nullptr; count = 0; }
    free(chunk_offs);
    free(sample_szs);
    free(sc);
    return ok;
}

bool M4aDemux::_parse_audio_entry(uint32_t off, uint32_t len) {
    uint8_t b[64];
    if (fseeko(_fp, (off_t)off, SEEK_SET) != 0 || fread(b, 1, 40, _fp) != 40) {
        snprintf(_err, sizeof(_err), "stsd entry short");
        return false;
    }
    /* entry: [size:4][type:4]... mp4a 的 sample entry：
     * reserved[6] + data_ref:2 + version:2 + rev:2 + vendor:4 +
     * channels:2 + samplesize:2 + predef:2 + reserved:2 + samplerate:4(16.16) */
    if (rd32(b + 4) != FCC('m', 'p', '4', 'a')) {
        snprintf(_err, sizeof(_err), "unsupported codec");
        return false;
    }
    channels = (uint8_t)(rd32(b + 24) >> 16);        /* b[24..25]: channelcount（大端高 16 位） */
    sample_rate = rd32(b + 32) >> 16;                /* b[32..35]: samplerate(16.16) */
    if (sample_rate == 0) sample_rate = 44100;
    if (channels == 0) channels = 2;
    return true;
}

uint32_t M4aDemux::read_sample(size_t i, uint8_t* buf, uint32_t cap) const {
    if (i >= count || !_fp) return 0;
    uint32_t sz = samples[i].size;
    if (sz > cap) sz = cap;
    if (fseeko(_fp, (off_t)samples[i].offset, SEEK_SET) != 0) return 0;
    return (uint32_t)fread(buf, 1, sz, _fp);
}

/* ADTS 头生成（供调用方把裸 AAC 帧包装成 ADTS 流） */
void m4a_make_adts(uint8_t* hdr, uint32_t sample_rate, uint8_t channels, uint32_t frame_len) {
    uint8_t freq = adts_freq_index(sample_rate);
    uint8_t ch = channels > 7 ? 2 : channels;
    hdr[0] = 0xFF;
    hdr[1] = 0xF1;                                    /* MPEG-4, layer 0, 无 CRC */
    hdr[2] = (1 << 6) | (freq << 2) | (ch >> 2);      /* profile=LC(AOT2-1=1), freq idx, ch 高位 */
    hdr[3] = ((ch & 3) << 6) | (uint8_t)((frame_len >> 11) & 0x03);
    hdr[4] = (uint8_t)((frame_len >> 3) & 0xFF);
    hdr[5] = (uint8_t)(((frame_len & 0x07) << 5) | 0x1F);
    hdr[6] = 0xFC;
}

} // namespace drivers
} // namespace memoria
