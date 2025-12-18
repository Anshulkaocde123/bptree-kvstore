#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include "config.h"
#include "disk_manager.h"
#include <list>
#include <unordered_map>

struct Page {
    int page_id = -1;
    char data[PAGE_SIZE];
    bool is_dirty = false;
    int pin_count = 0;
};

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager *disk_manager);
    ~BufferPoolManager();

    Page *FetchPage(int page_id);
    bool UnpinPage(int page_id, bool is_dirty);
    bool FlushPage(int page_id);
    Page *NewPage(int *page_id);
    bool DeletePage(int page_id);
    void FlushAllPages();
    DiskManager *GetDiskManager() const { return disk_manager_; }

private:
    size_t pool_size_;
    DiskManager *disk_manager_;
    Page *pages_;
    std::unordered_map<int, size_t> page_table_;
    std::list<size_t> free_list_;
    std::list<size_t> lru_list_;
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_map_;

    size_t FindVictimPage();
};

#endif // BUFFER_POOL_MANAGER_H
