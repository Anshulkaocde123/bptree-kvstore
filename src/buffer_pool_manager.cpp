#include "buffer_pool_manager.h"
#include <algorithm>

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    pages_ = new Page[pool_size_];
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(i);
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
    delete[] pages_;
}

Page *BufferPoolManager::FetchPage(int page_id) {
    // Check if page is already in buffer pool
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        size_t frame_id = it->second;
        Page *page = &pages_[frame_id];
        page->pin_count++;

        // Move to front of LRU (most recently used)
        if (lru_map_.count(frame_id)) {
            lru_list_.erase(lru_map_[frame_id]);
            lru_map_.erase(frame_id);
        }
        return page;
    }

    // Page not in pool, need to fetch from disk
    size_t frame_id = FindVictimPage();
    if (frame_id == pool_size_) {
        return nullptr;  // No available frame
    }

    Page *page = &pages_[frame_id];

    // If victim page is dirty, flush it
    if (page->page_id != -1) {
        if (page->is_dirty) {
            disk_manager_->WritePage(page->page_id, page->data);
        }
        page_table_.erase(page->page_id);
    }

    // Read new page from disk
    page->page_id = page_id;
    page->is_dirty = false;
    page->pin_count = 1;
    disk_manager_->ReadPage(page_id, page->data);

    page_table_[page_id] = frame_id;
    return page;
}

bool BufferPoolManager::UnpinPage(int page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    size_t frame_id = it->second;
    Page *page = &pages_[frame_id];

    if (page->pin_count <= 0) {
        return false;
    }

    page->pin_count--;
    if (is_dirty) {
        page->is_dirty = true;
    }

    // Add to LRU list when pin_count becomes 0
    if (page->pin_count == 0) {
        lru_list_.push_front(frame_id);
        lru_map_[frame_id] = lru_list_.begin();
    }

    return true;
}

bool BufferPoolManager::FlushPage(int page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return false;
    }

    size_t frame_id = it->second;
    Page *page = &pages_[frame_id];
    disk_manager_->WritePage(page->page_id, page->data);
    page->is_dirty = false;
    return true;
}

Page *BufferPoolManager::NewPage(int *page_id) {
    size_t frame_id = FindVictimPage();
    if (frame_id == pool_size_) {
        return nullptr;
    }

    Page *page = &pages_[frame_id];

    // Flush victim if dirty
    if (page->page_id != -1) {
        if (page->is_dirty) {
            disk_manager_->WritePage(page->page_id, page->data);
        }
        page_table_.erase(page->page_id);
    }

    *page_id = disk_manager_->AllocatePage();
    page->page_id = *page_id;
    page->is_dirty = false;
    page->pin_count = 1;
    std::fill(page->data, page->data + PAGE_SIZE, 0);

    page_table_[*page_id] = frame_id;
    return page;
}

bool BufferPoolManager::DeletePage(int page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return true;  // Page not in pool
    }

    size_t frame_id = it->second;
    Page *page = &pages_[frame_id];

    if (page->pin_count > 0) {
        return false;  // Cannot delete pinned page
    }

    // Remove from LRU
    if (lru_map_.count(frame_id)) {
        lru_list_.erase(lru_map_[frame_id]);
        lru_map_.erase(frame_id);
    }

    page_table_.erase(page_id);
    page->page_id = -1;
    page->is_dirty = false;
    page->pin_count = 0;
    free_list_.push_back(frame_id);

    return true;
}

void BufferPoolManager::FlushAllPages() {
    for (auto &[page_id, frame_id] : page_table_) {
        Page *page = &pages_[frame_id];
        if (page->is_dirty) {
            disk_manager_->WritePage(page->page_id, page->data);
            page->is_dirty = false;
        }
    }
}

size_t BufferPoolManager::FindVictimPage() {
    // First check free list
    if (!free_list_.empty()) {
        size_t frame_id = free_list_.front();
        free_list_.pop_front();
        return frame_id;
    }

    // Use LRU eviction - back of list is least recently used
    while (!lru_list_.empty()) {
        size_t frame_id = lru_list_.back();
        lru_list_.pop_back();
        lru_map_.erase(frame_id);

        Page *page = &pages_[frame_id];
        if (page->pin_count == 0) {
            return frame_id;
        }
    }

    return pool_size_;  // No victim found
}
