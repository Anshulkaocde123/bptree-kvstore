# B+ Tree Storage Engine - Development Guide

## Quick Start

### Building

```bash
cd bptree-kvstore
make clean
make
```

### Running Tests

```bash
./bptree_kvstore
```

This runs all 4 test phases with 10,000 keys.

## Code Organization

### `src/config.h`
Central configuration file with constants:
```cpp
PAGE_SIZE = 4096 bytes        // Standard disk page size
MAX_PAGES_IN_RAM = 64         // Buffer pool frames
```

### `src/disk_manager.h/cpp`
Handles persistent storage:
- `ReadPage(page_id, buffer)` - Read page from disk
- `WritePage(page_id, buffer)` - Write page to disk
- `AllocatePage()` - Allocate new page
- `GetNumPages()` - Get total pages in database

### `src/buffer_pool_manager.h/cpp`
Manages in-memory pages with LRU eviction:
- `FetchPage(page_id)` - Get page (pins it)
- `UnpinPage(page_id, dirty)` - Release page
- `NewPage(page_id*)` - Allocate new page
- `FlushPage(page_id)` - Write page to disk
- `FindVictimPage()` - LRU eviction

### `src/btree.h/cpp`
B+ tree implementation:
- `Insert(key, value)` - O(log n) insertion
- `Search(key)` - O(log n) lookup
- `Remove(key)` - Lazy deletion
- `Scan(start_key, end_key)` - Range query
- `LoadMetaPage()` - Recovery from disk
- `UpdateMetaPage()` - Persist root to Page 0

### `src/main.cpp`
Comprehensive test suite with 4 phases

## Key Data Structures

### LeafEntry
```cpp
struct LeafEntry {
    int key;
    char value[128];  // VALUE_SIZE
};
```

### LeafPageHeader
```cpp
struct LeafPageHeader {
    BPlusTreePageHeader base;
    int next_page_id;  // Linked list pointer
};
```

### BPlusTreePageHeader
```cpp
struct BPlusTreePageHeader {
    PageType page_type;  // LEAF or INTERNAL
    int num_keys;
    int parent_page_id;
};
```

## Algorithm Walkthrough

### Insertion

1. **Find Leaf Page**
   - Start at root (stored in meta page)
   - Traverse internal nodes using binary search
   - Follow child pointers until reaching leaf

2. **Insert into Leaf**
   - If leaf has space: insert directly
   - If leaf is full: split the leaf

3. **Leaf Split**
   - Create new leaf page
   - Distribute entries between old and new leaf
   - Promote middle key to parent
   - Recursively insert into parent

4. **Propagate Up**
   - If parent is full: split parent
   - Continue until reaching root or finding space
   - If root splits: create new root (increments tree height)

### Range Scan

1. **Find Starting Leaf**
   - Use `FindLeafPage(start_key)` for O(log n) traversal
   - Binary search within leaf to find start position

2. **Collect Results**
   - Iterate through leaf entries in range [start_key, end_key]
   - Skip deleted entries (empty values)
   - Use `next_page_id` to traverse to next leaf

3. **Return Results**
   - Results already sorted (B+ tree property)
   - Time complexity: O(log n + k) where k = results

### Lazy Deletion

1. **Mark Entry as Deleted**
   - Set value to empty (memset to 0)
   - Keep key in tree (no structure change)

2. **Skip During Search/Scan**
   - Check if `value[0] == '\0'`
   - Skip if deleted, continue searching
   - Results exclude deleted entries

3. **Advantages**
   - No expensive tree rebalancing
   - O(log n) deletion time
   - No immediate disk I/O

## Testing Strategy

### Unit Tests (Phase 1-4)
- Individual operations tested
- Error conditions handled
- Boundary cases verified

### Integration Tests
- Multiple operations in sequence
- Persistence and recovery
- Consistency across phases

### Stress Tests
- 10,000 keys with 64-frame buffer
- 156:1 oversubscription ratio
- LRU eviction under pressure

## Performance Tuning

### Buffer Pool Efficiency
- Increase `MAX_PAGES_IN_RAM` for larger datasets
- Monitor LRU hit ratio (fewer evictions = better)
- Pin pages only as long as needed

### Tree Efficiency
- Larger page size reduces tree height
- Smaller page size reduces I/O per split
- Default 4096 bytes is standard

### Disk I/O Optimization
- Batch writes to reduce fsync calls
- Consider page compression for larger datasets
- Use separate log for crash recovery

## Debugging Tips

### Enable Detailed Output
Modify `main.cpp` to print:
```cpp
std::cout << "Inserting key: " << key << std::endl;
```

### Track Page Allocations
Monitor with:
```cpp
std::cout << "Pages allocated: " << disk_manager->GetNumPages() << std::endl;
```

### Verify Tree Structure
Add debugging function:
```cpp
void PrintTreeStructure() {
    // Print all keys in sorted order
    // Useful for catching corruption
}
```

### Memory Leak Detection
Use valgrind:
```bash
valgrind --leak-check=full ./bptree_kvstore
```

## Common Issues & Solutions

### Issue: Too Many Evictions
**Symptom**: Slow performance with moderate data size
**Solution**: Increase `MAX_PAGES_IN_RAM` in config.h

### Issue: Keys Not Found After Deletion
**Symptom**: Search returns nullopt for non-deleted keys
**Solution**: Check if value is accidentally zeroed (lazy deletion mark)

### Issue: Memory Leaks
**Symptom**: Valgrind reports unreleased memory
**Solution**: Check all `UnpinPage()` calls match `FetchPage()` calls

### Issue: Corruption After Heavy Load
**Symptom**: Inconsistent results or crashes
**Solution**: Verify pin counts are properly managed in split operations

## Contributing

When adding features:
1. Maintain O(log n) or better complexity
2. Update tests in main.cpp
3. Verify persistence with reload test
4. Run stress test with 10k+ keys
5. Check for memory leaks with valgrind

## References

- B+ Tree properties and invariants
- LRU page replacement theory
- Buffer management strategies
- Page-based storage architecture
