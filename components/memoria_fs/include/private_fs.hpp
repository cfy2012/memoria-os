/**
 * @file private_fs.hpp
 * @brief 私有文件系统布局：Superblock / Entry Table / 数据堆
 */

#pragma once

#include "drivers_config.hpp"
#include <esp_partition.h>
#include <esp_err.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <functional>
#include <memory>

namespace memoria {
namespace fs {

enum class FileType : uint8_t {
    Unknown = 0, Photo, Note, Audio, Config, Index, EasterEgg, State,
};

inline constexpr uint32_t MAGIC       = 0x4D454D4FUL;
inline constexpr uint32_t VERSION     = 2;
inline constexpr uint32_t SB_SIZE     = 4096;
inline constexpr uint32_t ENTRY_SIZE  = 64;
inline constexpr uint32_t MAX_ENTRIES  = 1024;
inline constexpr uint32_t ET_SIZE     = MAX_ENTRIES * ENTRY_SIZE;
inline constexpr uint32_t DATA_OFFSET = SB_SIZE + ET_SIZE;
inline constexpr size_t   MEM_NAME_MAX    = 32;

#pragma pack(push, 1)
struct Entry {
    uint32_t    id         = 0;
    uint8_t     type       = static_cast<uint8_t>(FileType::Unknown);
    uint8_t     flags      = 0;
    uint16_t    _pad0      = 0;
    char        name[MEM_NAME_MAX] = {0};
    uint32_t    size       = 0;
    uint32_t    offset     = 0;
    uint32_t    crc32      = 0;
    uint32_t    created_at = 0;
    uint8_t        reserved[8] = {0};
};
#pragma pack(pop)
static_assert(sizeof(Entry) == 64, "Entry must be exactly 64 bytes");

/* ============================================================
 *  Superblock 布局约束：sizeof(Superblock) 必须精确等于 4096 字节。
 *  已知字段 8 × uint32_t = 32 字节，reserved[1016]（uint32_t）补齐
 *  余下 4064 字节；static_assert 为最终校验。
 * ============================================================ */
struct Superblock {
    uint32_t magic                = MAGIC;
    uint32_t version              = VERSION;
    uint32_t total_entries        = MAX_ENTRIES;
    uint32_t used_entries         = 0;
    uint32_t data_area_start      = DATA_OFFSET;
    uint32_t data_area_size       = 0;
    uint32_t next_free_offset     = DATA_OFFSET;
    uint32_t crc32_of_entry_table = 0;
    uint32_t reserved[1016]       = {0};   /* 4096 - 8*4 = 4064 / 4 = 1016 */
};
/* 布局最终校验：不通过时调整 reserved[] 长度 */
static_assert(sizeof(Superblock) == 4096,
              "Superblock must be exactly 4096 bytes; adjust reserved[] length");

/* FileHandle：文件只读视图，持有 Entry 引用与读写位置 */
class FileHandle {
public:
    FileHandle() = default;
    FileHandle(const Entry* e, uint32_t pos, bool write)
        : _entry(e), _pos(pos), _writing(write) {}
    bool          valid()   const { return _entry != nullptr; }
    bool          writing() const { return _writing; }
    uint32_t      pos()     const { return _pos; }
    void          seek(uint32_t p) { _pos = p; }
    const Entry&  entry()   const { return *_entry; }
private:
    const Entry* _entry = nullptr;
    uint32_t     _pos   = 0;
    bool         _writing = false;
};

using EntryVisitor = std::function<bool(const Entry& entry)>;

class PrivateFs {
public:
    static PrivateFs* instance();

    esp_err_t init();
    esp_err_t format();

    esp_err_t create(FileType type, const std::string& name,
                     uint32_t size = 0, Entry** out = nullptr);

    Entry* find_by_id(uint32_t id);
    Entry* find_by_name(const std::string& name);

    esp_err_t list_by_type(FileType type, EntryVisitor cb);

    esp_err_t open(uint32_t id, FileHandle& handle, bool write = false);
    int       read(FileHandle& handle, void* buf, uint32_t len);
    int       write(FileHandle& handle, const void* buf, uint32_t len);
    esp_err_t close(FileHandle& handle);

    esp_err_t remove(uint32_t id);

    void      stats(uint32_t& used_bytes, uint32_t& total_bytes,
                    uint32_t& used_entries, uint32_t& total_entries) const;

    esp_err_t sync();

    bool is_inited() const { return _inited; }
    ~PrivateFs() { if (_inited) sync(); }

private:
    PrivateFs() = default;

    esp_err_t _flash_read (uint32_t off, void* buf, size_t size);
    esp_err_t _flash_write(uint32_t off, const void* buf, size_t size);
    esp_err_t _flash_erase_range(uint32_t off, size_t size);
    esp_err_t _flash_erase_all();

    esp_err_t _sb_save();
    esp_err_t _et_save();

    uint32_t _next_id();
    int      _find_free_slot();
    uint32_t _crc32(const void* data, size_t len);

    const esp_partition_t* _partition = nullptr;
    bool _inited = false;

    Superblock _sb{};
    Entry      _entries[MAX_ENTRIES]{};
};

} // namespace fs
} // namespace memoria