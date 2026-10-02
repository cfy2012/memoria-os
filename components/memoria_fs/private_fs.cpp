/**
 * @file private_fs.cpp
 * @brief 自研私有文件系统核心实现
 *
 * 关键点：
 *   1. esp_partition_write 要求 offset 和 size 都是 4 字节对齐：本结构天然对齐
 *   2. Superblock/Entry Table 持久化前必须先 erase：直接调用 esp_partition_erase_range
 *   3. Data Heap 是"只扩展、不回收"策略（简单高效），删除条目仅标记 id=0
 *   4. 析构时自动 sync（RAII 保证掉电前写回）
 *   5. CRC32：完整 IEEE 802.3 查表实现（编译期静态表）
 */

#include "private_fs.hpp"

extern "C" {
#include <esp_partition.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <time.h>
}

#include <cstdio>

#include <algorithm>
#include <cstring>

namespace memoria {
namespace fs {

static const char* TAG = "MEM_FS";

PrivateFs* PrivateFs::instance() {
    static PrivateFs s_inst;
    return &s_inst;
}

/* ============================================================
 *  完整 CRC32 表（IEEE 802.3，编译期）
 * ============================================================ */
static const std::array<uint32_t, 256> CRC32_TABLE = [] {
    uint32_t t[256];
    for (int i = 0; i < 256; i++) {
        uint32_t c = static_cast<uint32_t>(i);
        for (int b = 0; b < 8; b++) {
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
        t[i] = c;
    }
    std::array<uint32_t, 256> r; for (int i = 0; i < 256; i++) r[i] = t[i]; return r;
}();

uint32_t PrivateFs::_crc32(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ CRC32_TABLE[(crc ^ p[i]) & 0xFF];
    }
    return ~crc;
}

/* ============================================================
 *  Flash 读写封装（带 4 字节对齐检查日志）
 * ============================================================ */
esp_err_t PrivateFs::_flash_read(uint32_t off, void* buf, size_t size) {
    return esp_partition_read(_partition, off, buf, size);
}

esp_err_t PrivateFs::_flash_write(uint32_t off, const void* buf, size_t size) {
    /* 对齐检查：不满足就返回错误而不是静默失败 */
    if ((off & 0x3) != 0 || (size & 0x3) != 0) {
        ESP_LOGE(TAG, "flash_write 4-byte align failed: off=0x%X size=%zu", off, size);
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_write(_partition, off, buf, size);
}

esp_err_t PrivateFs::_flash_erase_range(uint32_t off, size_t size) {
    return esp_partition_erase_range(_partition, off, size);
}

esp_err_t PrivateFs::_flash_erase_all() {
    return esp_partition_erase_range(_partition, 0, _partition->size);
}

/* ============================================================
 *  Superblock / Entry Table 持久化
 * ============================================================ */
esp_err_t PrivateFs::_sb_save() {
    _sb.magic = MAGIC;
    _sb.version = VERSION;
    _sb.total_entries = MAX_ENTRIES;

    /* Entry Table CRC（放 Superblock 末尾） */
    _sb.crc32_of_entry_table = _crc32(_entries, ET_SIZE);

    /* 必须先擦除再写入（Flash NOR 特性） */
    _flash_erase_range(0, SB_SIZE);
    return _flash_write(0, &_sb, sizeof(_sb));
}

esp_err_t PrivateFs::_et_save() {
    _flash_erase_range(SB_SIZE, ET_SIZE);
    return _flash_write(SB_SIZE, _entries, ET_SIZE);
}

/* ============================================================
 *  初始化 / 格式化
 * ============================================================ */
esp_err_t PrivateFs::init() {
    if (_inited) return ESP_OK;

    _partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           ESP_PARTITION_SUBTYPE_ANY,
                                           "mem_priv");
    if (!_partition) {
        ESP_LOGE(TAG, "partition 'mem_priv' not found");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "mem_priv: %u KB (0x%X)",
             (unsigned)(_partition->size / 1024), _partition->address);

    /* 读 Superblock */
    esp_err_t ret = _flash_read(0, &_sb, sizeof(_sb));
    if (ret != ESP_OK || _sb.magic != MAGIC || _sb.version != VERSION) {
        ESP_LOGW(TAG, "superblock invalid or wrong version → formatting");
        return format();
    }

    /* 读 Entry Table */
    ret = _flash_read(SB_SIZE, _entries, ET_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "entry table read failed");
        return ret;
    }

    /* 验证 Entry Table CRC */
    uint32_t calc_crc = _crc32(_entries, ET_SIZE);
    if (calc_crc != _sb.crc32_of_entry_table) {
        ESP_LOGW(TAG, "entry table CRC mismatch (stored=0x%08X calc=0x%08X) → keep loading",
                 _sb.crc32_of_entry_table, calc_crc);
        /* 不格式化：可能是写中断，条目可能还是有效的 */
    }

    /* 扫描统计 */
    uint32_t used = 0;
    for (auto& e : _entries) if (e.id != 0) used++;
    _sb.used_entries = used;

    _sb.data_area_start = DATA_OFFSET;
    _sb.data_area_size  = _partition->size - DATA_OFFSET;
    if (_sb.next_free_offset < DATA_OFFSET || _sb.next_free_offset > _partition->size) {
        ESP_LOGW(TAG, "next_free_offset out of range → reset to DATA_OFFSET");
        _sb.next_free_offset = DATA_OFFSET;
    }

    _inited = true;
    ESP_LOGI(TAG, "loaded: %u/%u entries used, data_heap=0x%X..0x%X",
             used, MAX_ENTRIES, DATA_OFFSET, _sb.next_free_offset);
    return ESP_OK;
}

esp_err_t PrivateFs::format() {
    ESP_LOGW(TAG, "formatting mem_priv (erase + write)...");

    _flash_erase_all();

    std::memset(&_sb, 0, sizeof(_sb));
    _sb.magic              = MAGIC;
    _sb.version            = VERSION;
    _sb.total_entries      = MAX_ENTRIES;
    _sb.data_area_start    = DATA_OFFSET;
    _sb.data_area_size     = _partition->size - DATA_OFFSET;
    _sb.next_free_offset   = DATA_OFFSET;

    std::memset(_entries, 0, sizeof(_entries));

    _sb_save();
    _et_save();

    _inited = true;
    ESP_LOGI(TAG, "format OK");
    return ESP_OK;
}

/* ============================================================
 *  条目管理
 * ============================================================ */
uint32_t PrivateFs::_next_id() {
    uint32_t seed = 0;
    for (auto& e : _entries) {
        if (e.id > seed) seed = e.id;
    }
    return ++seed;
}

int PrivateFs::_find_free_slot() {
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (_entries[i].id == 0) return i;
    }
    return -1;
}

esp_err_t PrivateFs::create(FileType type, const std::string& name,
                             uint32_t size, Entry** out) {
    if (!_inited) return ESP_ERR_INVALID_STATE;
    if (name.empty()) return ESP_ERR_INVALID_ARG;

    /* 截断过长名称 */
    std::string fixed_name = name;
    if (fixed_name.size() >= MEM_NAME_MAX) fixed_name.resize(MEM_NAME_MAX - 1);

    /* 同名已存在 → 返回已有 */
    Entry* exist = find_by_name(fixed_name);
    if (exist) {
        if (out) *out = exist;
        return ESP_OK;
    }

    int slot = _find_free_slot();
    if (slot < 0) {
        ESP_LOGE(TAG, "entry table full (%u)", MAX_ENTRIES);
        return ESP_ERR_NO_MEM;
    }

    /* 数据堆空间检查：Data Heap 写是 4 字节对齐的 */
    uint32_t size_aligned = (size + 3) & ~3U;  /* round up to 4B */
    if (_sb.next_free_offset + size_aligned > _partition->size) {
        ESP_LOGE(TAG, "data heap exhausted (need %u, free 0x%X..0x%X)",
                 size, _sb.next_free_offset, (unsigned)_partition->size);
        return ESP_ERR_NO_MEM;
    }

    Entry& e = _entries[slot];
    std::memset(&e, 0, sizeof(e));
    e.id         = _next_id();
    e.type       = static_cast<uint8_t>(type);
    std::memcpy(e.name, fixed_name.c_str(), fixed_name.size());
    e.size       = size;   /* 记录原始 size（读时知道真实长度） */
    e.offset     = _sb.next_free_offset;
    e.crc32      = 0;
    e.created_at = static_cast<uint32_t>(time(nullptr));

    _sb.next_free_offset += size_aligned;
    _sb.used_entries++;

    _sb_save();
    _et_save();

    ESP_LOGI(TAG, "created #%u '%s' type=%u size=%u off=0x%X",
             e.id, e.name, e.type, e.size, e.offset);
    if (out) *out = &e;
    return ESP_OK;
}

Entry* PrivateFs::find_by_id(uint32_t id) {
    if (!_inited || id == 0) return nullptr;
    for (auto& e : _entries) if (e.id == id) return &e;
    return nullptr;
}

Entry* PrivateFs::find_by_name(const std::string& name) {
    if (!_inited) return nullptr;
    for (auto& e : _entries) {
        if (e.id != 0 && e.name[0] != '\0' && std::strcmp(e.name, name.c_str()) == 0) {
            return &e;
        }
    }
    return nullptr;
}

esp_err_t PrivateFs::list_by_type(FileType type, EntryVisitor cb) {
    if (!_inited || !cb) return ESP_ERR_INVALID_ARG;
    for (auto& e : _entries) {
        if (e.id != 0 && static_cast<FileType>(e.type) == type) {
            if (!cb(e)) break;
        }
    }
    return ESP_OK;
}

esp_err_t PrivateFs::remove(uint32_t id) {
    Entry* e = find_by_id(id);
    if (!e) return ESP_ERR_NOT_FOUND;
    ESP_LOGI(TAG, "removed #%u '%s'", e->id, e->name);
    std::memset(e, 0, sizeof(*e));
    _sb.used_entries--;
    _sb_save();
    _et_save();
    return ESP_OK;
}

/* ============================================================
 *  打开 / 读 / 写 / 关闭
 * ============================================================ */
esp_err_t PrivateFs::open(uint32_t id, FileHandle& handle, bool write) {
    Entry* e = find_by_id(id);
    if (!e) return ESP_ERR_NOT_FOUND;
    handle = FileHandle(e, 0, write);
    return ESP_OK;
}

int PrivateFs::read(FileHandle& handle, void* buf, uint32_t len) {
    if (!handle.valid() || !buf) return -1;
    const Entry& e = handle.entry();
    if (e.size == 0) return 0;

    uint32_t remaining = e.size - handle.pos();
    uint32_t to_read   = std::min(len, remaining);
    if (to_read == 0) return 0;

    esp_err_t ret = _flash_read(e.offset + handle.pos(), buf, to_read);
    if (ret != ESP_OK) return -1;

    handle.seek(handle.pos() + to_read);
    return static_cast<int>(to_read);
}

int PrivateFs::write(FileHandle& handle, const void* buf, uint32_t len) {
    if (!handle.valid() || !handle.writing() || !buf) return -1;
    Entry& e = *const_cast<Entry*>(&handle.entry());

    uint32_t new_size_raw = handle.pos() + len;
    uint32_t new_size_aligned = (new_size_raw + 3) & ~3U;
    uint32_t cur_end_aligned  = (e.offset + ((e.size + 3) & ~3U));

    bool extends = (new_size_raw > e.size);
    uint32_t flash_write_size = (extends) ? (e.offset + new_size_aligned) - cur_end_aligned
                                          : len;  /* 覆盖场景：写原始长度 */

    /* 覆盖已有区域：直接按当前 pos 写 len 字节 */
    if (!extends) {
        esp_err_t ret = _flash_write(e.offset + handle.pos(), buf,
                                     (len & 0x3) ? (len + 4 - (len & 0x3)) : len);
        if (ret != ESP_OK) return -1;
        handle.seek(handle.pos() + len);
        return static_cast<int>(len);
    }

    /* 扩展：写新数据 + 更新 Superblock.next_free_offset */
    if (cur_end_aligned + flash_write_size > _partition->size) {
        ESP_LOGE(TAG, "data heap full during extend (need 0x%X)",
                 cur_end_aligned + flash_write_size);
        return -1;
    }
    esp_err_t ret = _flash_write(cur_end_aligned, buf,
                                 (len & 0x3) ? (len + 4 - (len & 0x3)) : len);
    if (ret != ESP_OK) return -1;

    e.size = new_size_raw;
    _sb.next_free_offset = cur_end_aligned + flash_write_size;
    handle.seek(new_size_raw);

    _sb_save();
    _et_save();
    return static_cast<int>(len);
}

esp_err_t PrivateFs::close(FileHandle& handle) {
    if (!handle.valid()) return ESP_ERR_INVALID_ARG;

    if (handle.writing()) {
        /* 写 CRC：读回整个条目数据做 CRC32 */
        const Entry& e = handle.entry();
        void* tmp = heap_caps_malloc((e.size & 0x3) ? (e.size + 4 - (e.size & 0x3)) : e.size,
                                     MALLOC_CAP_SPIRAM);
        if (tmp) {
            _flash_read(e.offset, tmp, (e.size & 0x3) ? (e.size + 4 - (e.size & 0x3)) : e.size);
            Entry* em = const_cast<Entry*>(&e);
            em->crc32 = _crc32(tmp, e.size);  /* 只算真实 size */
            std::free(tmp);
            _et_save();
        }
    }

    /* 标记句柄失效 */
    handle = FileHandle{};
    return ESP_OK;
}

/* ============================================================
 *  状态 & Sync
 * ============================================================ */
void PrivateFs::stats(uint32_t& used_bytes, uint32_t& total_bytes,
                      uint32_t& used_entries, uint32_t& total_entries) const {
    used_bytes    = _sb.next_free_offset - DATA_OFFSET;
    total_bytes   = _partition ? _partition->size : 0;
    used_entries  = _sb.used_entries;
    total_entries = _sb.total_entries;
}

esp_err_t PrivateFs::sync() {
    if (!_inited) return ESP_ERR_INVALID_STATE;
    esp_err_t r1 = _sb_save();
    esp_err_t r2 = _et_save();
    if (r1 != ESP_OK || r2 != ESP_OK) {
        ESP_LOGE(TAG, "sync failed: sb=%s et=%s",
                 esp_err_to_name(r1), esp_err_to_name(r2));
        return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace fs
} // namespace memoria