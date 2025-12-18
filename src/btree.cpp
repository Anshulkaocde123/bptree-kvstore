#include "btree.h"
#include <algorithm>
#include <utility>

BPlusTree::BPlusTree(BufferPoolManager *buffer_pool_manager)
    : buffer_pool_manager_(buffer_pool_manager), root_page_id_(INVALID_PAGE_ID) {
    LoadMetaPage();
}

BPlusTree::~BPlusTree() {
    // Ensure meta page is flushed
    buffer_pool_manager_->FlushPage(META_PAGE_ID);
}

// ==================== Meta Page ====================

void BPlusTree::LoadMetaPage() {
    // Check if database already has pages (not a fresh database)
    // If num_pages > 0, then page 0 (meta page) exists with tree metadata
    if (buffer_pool_manager_->GetDiskManager()->GetNumPages() > 0) {
        Page *meta = buffer_pool_manager_->FetchPage(META_PAGE_ID);
        if (meta) {
            MetaPage *meta_data = reinterpret_cast<MetaPage *>(meta->data);
            // Load the persisted root_page_id from the meta page
            root_page_id_ = meta_data->root_page_id;
            buffer_pool_manager_->UnpinPage(META_PAGE_ID, false);
        }
    }
}

void BPlusTree::UpdateMetaPage() {
    Page *meta = buffer_pool_manager_->FetchPage(META_PAGE_ID);
    if (meta) {
        MetaPage *meta_data = reinterpret_cast<MetaPage *>(meta->data);
        meta_data->root_page_id = root_page_id_;
        buffer_pool_manager_->UnpinPage(META_PAGE_ID, true);
    }
}

// ==================== Helper Functions ====================

LeafPageHeader *BPlusTree::GetLeafHeader(Page *page) {
    return reinterpret_cast<LeafPageHeader *>(page->data);
}

LeafEntry *BPlusTree::GetLeafEntries(Page *page) {
    return reinterpret_cast<LeafEntry *>(page->data + LEAF_HEADER_SIZE);
}

BPlusTreePageHeader *BPlusTree::GetInternalHeader(Page *page) {
    return reinterpret_cast<BPlusTreePageHeader *>(page->data);
}

int *BPlusTree::GetInternalChildren(Page *page) {
    // Layout: [header][children array][keys array]
    // Children: child[0], child[1], ..., child[n] (n+1 children for n keys)
    return reinterpret_cast<int *>(page->data + INTERNAL_HEADER_SIZE);
}

int *BPlusTree::GetInternalKeys(Page *page) {
    // Keys start after max children array
    // Max children = INTERNAL_MAX_KEYS + 1
    return reinterpret_cast<int *>(page->data + INTERNAL_HEADER_SIZE + (INTERNAL_MAX_KEYS + 1) * sizeof(int));
}

// Binary search in leaf to find index where key should be
int BPlusTree::LeafFindKey(Page *page, int key) {
    LeafPageHeader *header = GetLeafHeader(page);
    LeafEntry *entries = GetLeafEntries(page);
    int lo = 0, hi = header->base.num_keys;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (entries[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Find which child to follow in internal node
int BPlusTree::InternalFindChild(Page *page, int key) {
    BPlusTreePageHeader *header = GetInternalHeader(page);
    int *children = GetInternalChildren(page);
    int *keys = GetInternalKeys(page);
    int n = header->num_keys;

    // Binary search for the correct child
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (keys[mid] <= key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return children[lo];
}

// ==================== Search ====================

Page *BPlusTree::FindLeafPage(int key) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        return nullptr;
    }

    Page *page = buffer_pool_manager_->FetchPage(root_page_id_);
    if (!page) return nullptr;

    BPlusTreePageHeader *header = reinterpret_cast<BPlusTreePageHeader *>(page->data);

    while (header->page_type == PageType::INTERNAL) {
        int child_page_id = InternalFindChild(page, key);
        buffer_pool_manager_->UnpinPage(page->page_id, false);
        page = buffer_pool_manager_->FetchPage(child_page_id);
        if (!page) return nullptr;
        header = reinterpret_cast<BPlusTreePageHeader *>(page->data);
    }

    return page;
}

std::optional<std::string> BPlusTree::Search(int key) {
    Page *leaf = FindLeafPage(key);
    if (!leaf) return std::nullopt;

    LeafPageHeader *header = GetLeafHeader(leaf);
    LeafEntry *entries = GetLeafEntries(leaf);

    int idx = LeafFindKey(leaf, key);
    std::optional<std::string> result = std::nullopt;

    if (idx < header->base.num_keys && entries[idx].key == key) {
        // Check if entry is not deleted (lazy deletion: empty value means deleted)
        if (entries[idx].value[0] != '\0') {
            result = std::string(entries[idx].value);
        }
    }

    buffer_pool_manager_->UnpinPage(leaf->page_id, false);
    return result;
}

// ==================== Insert ====================

bool BPlusTree::LeafInsert(Page *page, int key, const std::string &value) {
    LeafPageHeader *header = GetLeafHeader(page);
    LeafEntry *entries = GetLeafEntries(page);

    int idx = LeafFindKey(page, key);

    // Check for duplicate
    if (idx < header->base.num_keys && entries[idx].key == key) {
        // Update existing value
        std::memset(entries[idx].value, 0, VALUE_SIZE);
        std::strncpy(entries[idx].value, value.c_str(), VALUE_SIZE - 1);
        return true;
    }

    // Shift entries to make room
    for (int i = header->base.num_keys; i > idx; --i) {
        entries[i] = entries[i - 1];
    }

    // Insert new entry
    entries[idx].key = key;
    std::memset(entries[idx].value, 0, VALUE_SIZE);
    std::strncpy(entries[idx].value, value.c_str(), VALUE_SIZE - 1);
    header->base.num_keys++;

    return true;
}

void BPlusTree::InternalInsert(Page *page, int key, int right_child_id) {
    BPlusTreePageHeader *header = GetInternalHeader(page);
    int *children = GetInternalChildren(page);
    int *keys = GetInternalKeys(page);
    int n = header->num_keys;

    // Find position to insert
    int idx = 0;
    while (idx < n && keys[idx] < key) {
        idx++;
    }

    // Shift keys and children
    for (int i = n; i > idx; --i) {
        keys[i] = keys[i - 1];
        children[i + 1] = children[i];
    }

    // Insert
    keys[idx] = key;
    children[idx + 1] = right_child_id;
    header->num_keys++;
}

void BPlusTree::CreateNewRoot(Page *left_page, int key, Page *right_page) {
    int new_root_id;
    Page *new_root = buffer_pool_manager_->NewPage(&new_root_id);

    BPlusTreePageHeader *header = GetInternalHeader(new_root);
    header->page_type = PageType::INTERNAL;
    header->num_keys = 1;
    header->parent_page_id = INVALID_PAGE_ID;

    int *children = GetInternalChildren(new_root);
    int *keys = GetInternalKeys(new_root);

    children[0] = left_page->page_id;
    children[1] = right_page->page_id;
    keys[0] = key;

    // Update parent pointers
    BPlusTreePageHeader *left_header = reinterpret_cast<BPlusTreePageHeader *>(left_page->data);
    BPlusTreePageHeader *right_header = reinterpret_cast<BPlusTreePageHeader *>(right_page->data);
    left_header->parent_page_id = new_root_id;
    right_header->parent_page_id = new_root_id;

    root_page_id_ = new_root_id;
    UpdateMetaPage();  // Persist new root to meta page
    buffer_pool_manager_->UnpinPage(new_root_id, true);
}

void BPlusTree::SplitLeaf(Page *leaf_page, int key, const std::string &value) {
    LeafPageHeader *old_header = GetLeafHeader(leaf_page);
    LeafEntry *old_entries = GetLeafEntries(leaf_page);

    // Create temporary array with all entries including new one
    LeafEntry temp[LEAF_MAX_ENTRIES + 1];
    int idx = LeafFindKey(leaf_page, key);
    int j = 0;
    for (int i = 0; i < old_header->base.num_keys; ++i) {
        if (i == idx) {
            temp[j].key = key;
            std::memset(temp[j].value, 0, VALUE_SIZE);
            std::strncpy(temp[j].value, value.c_str(), VALUE_SIZE - 1);
            j++;
        }
        temp[j++] = old_entries[i];
    }
    if (idx == old_header->base.num_keys) {
        temp[j].key = key;
        std::memset(temp[j].value, 0, VALUE_SIZE);
        std::strncpy(temp[j].value, value.c_str(), VALUE_SIZE - 1);
        j++;
    }
    int total = j;

    // Create new leaf page
    int new_leaf_id;
    Page *new_leaf = buffer_pool_manager_->NewPage(&new_leaf_id);
    LeafPageHeader *new_header = GetLeafHeader(new_leaf);
    LeafEntry *new_entries = GetLeafEntries(new_leaf);

    // Split: left gets first half, right gets second half
    int split = total / 2;

    // Update old leaf
    old_header->base.num_keys = split;
    for (int i = 0; i < split; ++i) {
        old_entries[i] = temp[i];
    }

    // Initialize new leaf
    new_header->base.page_type = PageType::LEAF;
    new_header->base.num_keys = total - split;
    new_header->base.parent_page_id = old_header->base.parent_page_id;
    new_header->next_page_id = old_header->next_page_id;
    old_header->next_page_id = new_leaf_id;

    for (int i = split; i < total; ++i) {
        new_entries[i - split] = temp[i];
    }

    // Copy up the first key of the new leaf
    int middle_key = new_entries[0].key;

    // Insert into parent (still needs access to new_leaf's page_id)
    InsertIntoParent(leaf_page, middle_key, new_leaf);
    
    // Unpin new leaf only after InsertIntoParent is complete
    buffer_pool_manager_->UnpinPage(new_leaf_id, true);
}

void BPlusTree::SplitInternal(Page *internal_page, int key, int right_child_id) {
    BPlusTreePageHeader *old_header = GetInternalHeader(internal_page);
    int *old_children = GetInternalChildren(internal_page);
    int *old_keys = GetInternalKeys(internal_page);
    int n = old_header->num_keys;

    // Create temporary arrays
    int temp_keys[INTERNAL_MAX_KEYS + 1];
    int temp_children[INTERNAL_MAX_KEYS + 2];

    // Find position to insert
    int idx = 0;
    while (idx < n && old_keys[idx] < key) {
        idx++;
    }

    // Copy keys
    int j = 0;
    for (int i = 0; i < n; ++i) {
        if (i == idx) {
            temp_keys[j++] = key;
        }
        temp_keys[j++] = old_keys[i];
    }
    if (idx == n) {
        temp_keys[j++] = key;
    }
    int total_keys = j;

    // Copy children
    j = 0;
    for (int i = 0; i <= n; ++i) {
        if (i == idx + 1) {
            temp_children[j++] = right_child_id;
        }
        temp_children[j++] = old_children[i];
    }
    if (idx + 1 == n + 1) {
        temp_children[j++] = right_child_id;
    }

    // Split point: middle key moves up
    int split = total_keys / 2;
    int middle_key = temp_keys[split];

    // Create new internal page
    int new_internal_id;
    Page *new_internal = buffer_pool_manager_->NewPage(&new_internal_id);
    BPlusTreePageHeader *new_header = GetInternalHeader(new_internal);
    int *new_children = GetInternalChildren(new_internal);
    int *new_keys = GetInternalKeys(new_internal);

    // Update old internal page
    old_header->num_keys = split;
    for (int i = 0; i < split; ++i) {
        old_keys[i] = temp_keys[i];
        old_children[i] = temp_children[i];
    }
    old_children[split] = temp_children[split];

    // Initialize new internal page (keys after middle)
    new_header->page_type = PageType::INTERNAL;
    new_header->num_keys = total_keys - split - 1;
    new_header->parent_page_id = old_header->parent_page_id;

    for (int i = split + 1; i < total_keys; ++i) {
        new_keys[i - split - 1] = temp_keys[i];
        new_children[i - split - 1] = temp_children[i];
    }
    new_children[new_header->num_keys] = temp_children[total_keys];

    // Update parent pointers of children moved to new page
    for (int i = 0; i <= new_header->num_keys; ++i) {
        int child_id = new_children[i];
        Page *child = buffer_pool_manager_->FetchPage(child_id);
        BPlusTreePageHeader *child_header = reinterpret_cast<BPlusTreePageHeader *>(child->data);
        child_header->parent_page_id = new_internal_id;
        buffer_pool_manager_->UnpinPage(child_id, true);
    }

    // Insert middle key into parent (still needs access to new_internal's page_id)
    InsertIntoParent(internal_page, middle_key, new_internal);
    
    // Unpin new internal only after InsertIntoParent is complete
    buffer_pool_manager_->UnpinPage(new_internal_id, true);
}

void BPlusTree::InsertIntoParent(Page *left_page, int key, Page *right_page) {
    BPlusTreePageHeader *left_header = reinterpret_cast<BPlusTreePageHeader *>(left_page->data);

    // If left is root, create new root
    if (left_header->parent_page_id == INVALID_PAGE_ID) {
        CreateNewRoot(left_page, key, right_page);
        return;
    }

    // Fetch parent
    Page *parent = buffer_pool_manager_->FetchPage(left_header->parent_page_id);
    BPlusTreePageHeader *parent_header = GetInternalHeader(parent);

    // Update right page's parent
    BPlusTreePageHeader *right_header = reinterpret_cast<BPlusTreePageHeader *>(right_page->data);
    right_header->parent_page_id = parent->page_id;

    if (parent_header->num_keys < static_cast<int>(INTERNAL_MAX_KEYS)) {
        // Parent has room
        InternalInsert(parent, key, right_page->page_id);
        buffer_pool_manager_->UnpinPage(parent->page_id, true);
    } else {
        // Need to split parent
        SplitInternal(parent, key, right_page->page_id);
        buffer_pool_manager_->UnpinPage(parent->page_id, true);
    }
}

bool BPlusTree::Insert(int key, const std::string &value) {
    // Empty tree: create root leaf
    if (root_page_id_ == INVALID_PAGE_ID) {
        // First, allocate meta page (page 0) if this is a fresh tree
        int meta_id;
        Page *meta = buffer_pool_manager_->NewPage(&meta_id);
        if (meta) {
            // Initialize meta page to all zeros
            std::fill(meta->data, meta->data + PAGE_SIZE, 0);
            buffer_pool_manager_->UnpinPage(meta_id, true);  // Mark dirty to initialize on disk
        }

        Page *root = buffer_pool_manager_->NewPage(&root_page_id_);
        LeafPageHeader *header = GetLeafHeader(root);
        header->base.page_type = PageType::LEAF;
        header->base.num_keys = 0;
        header->base.parent_page_id = INVALID_PAGE_ID;
        header->next_page_id = INVALID_PAGE_ID;

        LeafInsert(root, key, value);
        UpdateMetaPage();  // Persist root_page_id to meta page
        buffer_pool_manager_->UnpinPage(root_page_id_, true);
        return true;
    }

    // Find leaf page
    Page *leaf = FindLeafPage(key);
    if (!leaf) return false;

    LeafPageHeader *header = GetLeafHeader(leaf);

    if (header->base.num_keys < static_cast<int>(LEAF_MAX_ENTRIES)) {
        // Leaf has room
        LeafInsert(leaf, key, value);
        buffer_pool_manager_->UnpinPage(leaf->page_id, true);
    } else {
        // Need to split
        SplitLeaf(leaf, key, value);
        buffer_pool_manager_->UnpinPage(leaf->page_id, true);
    }

    return true;
}

bool BPlusTree::Remove(int key) {
    // Lazy deletion: mark entry as deleted rather than physically removing it
    // This avoids expensive tree rebalancing operations
    
    if (root_page_id_ == INVALID_PAGE_ID) {
        return false;  // Tree is empty
    }

    // Find the leaf page containing the key
    Page *leaf = FindLeafPage(key);
    if (!leaf) {
        return false;
    }

    LeafPageHeader *header = GetLeafHeader(leaf);
    LeafEntry *entries = GetLeafEntries(leaf);

    // Search for the key in the leaf
    int idx = LeafFindKey(leaf, key);

    // Check if key exists
    if (idx >= header->base.num_keys || entries[idx].key != key) {
        buffer_pool_manager_->UnpinPage(leaf->page_id, false);
        return false;  // Key not found
    }

    // Lazy deletion: mark the value as deleted by setting it to empty
    std::memset(entries[idx].value, 0, VALUE_SIZE);

    // Mark page as dirty and unpin
    buffer_pool_manager_->UnpinPage(leaf->page_id, true);
    return true;
}

// ==================== Range Scan ====================

std::vector<std::pair<int, std::string>> BPlusTree::Scan(int start_key, int end_key) {
    std::vector<std::pair<int, std::string>> results;

    if (root_page_id_ == INVALID_PAGE_ID) {
        return results;
    }

    // Find leaf containing start_key using tree traversal
    Page *leaf = FindLeafPage(start_key);
    if (!leaf) {
        return results;
    }

    while (leaf) {
        LeafPageHeader *header = GetLeafHeader(leaf);
        LeafEntry *entries = GetLeafEntries(leaf);

        // Find starting position in this leaf only on the first iteration
        int start_idx = 0;
        if (results.empty()) {
            start_idx = LeafFindKey(leaf, start_key);
        }

        for (int i = start_idx; i < header->base.num_keys; ++i) {
            // Stop if we've exceeded the end_key
            if (entries[i].key > end_key) {
                buffer_pool_manager_->UnpinPage(leaf->page_id, false);
                return results;
            }
            // Include entry if it's >= start_key and not deleted (lazy deletion: empty value means deleted)
            if (entries[i].key >= start_key && entries[i].value[0] != '\0') {
                results.emplace_back(entries[i].key, std::string(entries[i].value));
            }
        }

        // Save next_page_id before unpinning
        int next_page_id = header->next_page_id;
        
        // Unpin the current leaf page immediately after use
        buffer_pool_manager_->UnpinPage(leaf->page_id, false);

        // Check if there's a next leaf to traverse
        if (next_page_id == INVALID_PAGE_ID) {
            break;
        }

        // Fetch the next leaf page in the linked list
        leaf = buffer_pool_manager_->FetchPage(next_page_id);
        if (!leaf) {
            // Handle case where FetchPage fails
            break;
        }
    }

    return results;
}
