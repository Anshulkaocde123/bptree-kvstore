#ifndef BTREE_H
#define BTREE_H

#include "buffer_pool_manager.h"
#include <optional>
#include <string>
#include <cstring>
#include <vector>
#include <utility>

constexpr size_t VALUE_SIZE = 128;
constexpr int INVALID_PAGE_ID = -1;
constexpr int META_PAGE_ID = 0;

// Meta page structure (Page 0)
struct MetaPage {
    int root_page_id;
};

enum class PageType : int {
    INVALID = 0,
    LEAF = 1,
    INTERNAL = 2
};

// Common header for all B+ tree pages
struct BPlusTreePageHeader {
    PageType page_type;
    int num_keys;
    int parent_page_id;
};

// Leaf page: header + next_page_id + array of (key, value) pairs
struct LeafPageHeader {
    BPlusTreePageHeader base;
    int next_page_id;
};

// Leaf entry
struct LeafEntry {
    int key;
    char value[VALUE_SIZE];
};

// Calculate max entries per page
constexpr size_t LEAF_HEADER_SIZE = sizeof(LeafPageHeader);
constexpr size_t LEAF_ENTRY_SIZE = sizeof(LeafEntry);
constexpr size_t LEAF_MAX_ENTRIES = (PAGE_SIZE - LEAF_HEADER_SIZE) / LEAF_ENTRY_SIZE;

// Internal page: header + array of children (n+1) interleaved with keys (n)
// Layout: [header][child0][key0][child1][key1]...[keyN-1][childN]
constexpr size_t INTERNAL_HEADER_SIZE = sizeof(BPlusTreePageHeader);
constexpr size_t INTERNAL_MAX_KEYS = (PAGE_SIZE - INTERNAL_HEADER_SIZE - sizeof(int)) / (2 * sizeof(int));

class BPlusTree {
public:
    explicit BPlusTree(BufferPoolManager *buffer_pool_manager);
    ~BPlusTree();

    bool Insert(int key, const std::string &value);
    bool Remove(int key);
    std::optional<std::string> Search(int key);
    std::vector<std::pair<int, std::string>> Scan(int start_key, int end_key);

    bool IsEmpty() const { return root_page_id_ == INVALID_PAGE_ID; }

private:
    BufferPoolManager *buffer_pool_manager_;
    int root_page_id_;

    // Helper functions for leaf pages
    LeafPageHeader *GetLeafHeader(Page *page);
    LeafEntry *GetLeafEntries(Page *page);
    int LeafFindKey(Page *page, int key);
    bool LeafInsert(Page *page, int key, const std::string &value);

    // Helper functions for internal pages
    BPlusTreePageHeader *GetInternalHeader(Page *page);
    int *GetInternalChildren(Page *page);
    int *GetInternalKeys(Page *page);
    int InternalFindChild(Page *page, int key);
    void InternalInsert(Page *page, int key, int right_child_id);

    // Tree operations
    Page *FindLeafPage(int key);
    void InsertIntoParent(Page *left_page, int key, Page *right_page);
    void SplitLeaf(Page *leaf_page, int key, const std::string &value);
    void SplitInternal(Page *internal_page, int key, int right_child_id);
    void CreateNewRoot(Page *left_page, int key, Page *right_page);

    // Meta page operations
    void LoadMetaPage();
    void UpdateMetaPage();
};

#endif // BTREE_H
