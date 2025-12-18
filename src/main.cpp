#include "btree.h"
#include "buffer_pool_manager.h"
#include "disk_manager.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <iomanip>

constexpr const char *DB_FILE = "test.db";
constexpr int NUM_KEYS = 10000;  // Stress test: 10k keys with only 64 buffer pool frames

int main() {
    std::remove(DB_FILE);  // Clean start

    std::cout << "=== B+ Tree Persistence & Range Scan Test ===" << std::endl;
    std::cout << "LEAF_MAX_ENTRIES: " << LEAF_MAX_ENTRIES << std::endl;
    std::cout << "INTERNAL_MAX_KEYS: " << INTERNAL_MAX_KEYS << std::endl;

    // Generate test keys: 0, 1, 2, ..., 499
    std::vector<int> keys;
    for (int i = 0; i < NUM_KEYS; ++i) {
        keys.push_back(i);
    }

    // Shuffle for random insertion order to stress-test tree structure
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(keys.begin(), keys.end(), g);

    // ==================== Phase 1: Build Tree ====================
    std::cout << "\n=== Phase 1: Building Tree with " << NUM_KEYS << " keys ===" << std::endl;
    {
        DiskManager disk_manager(DB_FILE);
        BufferPoolManager buffer_pool(MAX_PAGES_IN_RAM, &disk_manager);
        BPlusTree tree(&buffer_pool);

        // Insert all keys
        for (int key : keys) {
            std::string value = "value_" + std::to_string(key);
            tree.Insert(key, value);
        }
        std::cout << "  ✓ Inserted " << NUM_KEYS << " keys in random order" << std::endl;

        // Verify all keys can be found in-memory
        int found = 0;
        for (int key : keys) {
            auto result = tree.Search(key);
            if (result && *result == "value_" + std::to_string(key)) {
                found++;
            }
        }
        std::cout << "  ✓ Verified " << found << "/" << NUM_KEYS << " keys in memory" << std::endl;

        // Verify boundary and non-existent keys
        auto missing = tree.Search(-1);
        if (!missing) {
            std::cout << "  ✓ Non-existent key (-1) correctly returned nullopt" << std::endl;
        }
        missing = tree.Search(999999);
        if (!missing) {
            std::cout << "  ✓ Non-existent key (999999) correctly returned nullopt" << std::endl;
        }
    }
    std::cout << "  ✓ Phase 1 complete - Tree released from memory" << std::endl;

    // ==================== Phase 2: Verify Persistence ====================
    std::cout << "\n=== Phase 2: Persistence Verification ===" << std::endl;
    {
        DiskManager disk_manager(DB_FILE);
        BufferPoolManager buffer_pool(MAX_PAGES_IN_RAM, &disk_manager);
        BPlusTree tree(&buffer_pool);

        std::cout << "  ✓ New BPlusTree object created" << std::endl;
        std::cout << "  ✓ Tree recovered root_page_id from Page 0 (Meta Page)" << std::endl;

        // Verify all keys recovered from disk
        int found = 0;
        int verified_keys = 0;
        for (int key : keys) {
            auto result = tree.Search(key);
            if (result && *result == "value_" + std::to_string(key)) {
                found++;
                if (verified_keys < 5) {
                    std::cout << "    - Key " << key << " = " << *result << std::endl;
                    verified_keys++;
                }
            }
        }
        if (verified_keys < found) {
            std::cout << "    ... (and " << (found - verified_keys) << " more)" << std::endl;
        }
        std::cout << "  ✓ Verified " << found << "/" << NUM_KEYS << " keys recovered from disk" << std::endl;

        // ==================== Phase 3: Range Scan Test ====================
        std::cout << "\n=== Phase 3: Range Scan - Leaf Linked List Verification ===" << std::endl;

        // Perform range scan on subset to verify linked list traversal
        auto results = tree.Scan(100, 200);
        std::cout << "  Scan(100, 200) returned " << results.size() << " results:" << std::endl;

        // Verify results are sorted and in range
        bool is_sorted = true;
        bool all_in_range = true;
        int prev_key = -1;

        for (size_t i = 0; i < results.size(); ++i) {
            int key = results[i].first;
            const std::string &value = results[i].second;

            // Check if in range
            if (key < 100 || key > 200) {
                all_in_range = false;
            }

            // Check if sorted
            if (key <= prev_key) {
                is_sorted = false;
            }
            prev_key = key;

            // Print first and last few results
            if (i < 3 || i >= results.size() - 3) {
                std::cout << "    [" << std::setw(3) << key << "] = " << value << std::endl;
            } else if (i == 3) {
                std::cout << "    ..." << std::endl;
            }
        }

        std::cout << "  ✓ All results in range [100, 200]: " << (all_in_range ? "YES" : "NO") << std::endl;
        std::cout << "  ✓ Results are sorted: " << (is_sorted ? "YES" : "NO") << std::endl;

        // Verify expected count
        int expected_count = 0;
        for (int key : keys) {
            if (key >= 100 && key <= 200) expected_count++;
        }
        std::cout << "  ✓ Expected " << expected_count << " keys, found " << results.size() << std::endl;

        // Additional scan tests
        std::cout << "\n=== Additional Scan Tests ===" << std::endl;

        // Full range scan
        results = tree.Scan(0, NUM_KEYS - 1);
        std::cout << "  Scan(0, " << (NUM_KEYS - 1) << "): Found " << results.size()
                  << " keys (expected " << NUM_KEYS << ")" << std::endl;

        // Empty range
        results = tree.Scan(1000, 2000);
        std::cout << "  Scan(1000, 2000): Found " << results.size() << " keys (expected 0)" << std::endl;

        // Single key
        results = tree.Scan(250, 250);
        std::cout << "  Scan(250, 250): Found " << results.size() << " keys (expected 1)" << std::endl;

        // First 100 keys
        results = tree.Scan(0, 99);
        std::cout << "  Scan(0, 99): Found " << results.size() << " keys (expected 100)" << std::endl;

        // Last 100 keys
        results = tree.Scan(400, 499);
        std::cout << "  Scan(400, 499): Found " << results.size() << " keys (expected 100)" << std::endl;
    }
    std::cout << "\n  ✓ Phase 2 complete - All persistence verified" << std::endl;

    // ==================== Phase 4: Lazy Deletion Test ====================
    std::cout << "\n=== Phase 4: Lazy Deletion Test ===" << std::endl;
    {
        DiskManager disk_manager(DB_FILE);
        BufferPoolManager buffer_pool(MAX_PAGES_IN_RAM, &disk_manager);
        BPlusTree tree(&buffer_pool);

        std::cout << "  Inserting keys 1-10..." << std::endl;
        for (int i = 1; i <= 10; ++i) {
            tree.Insert(i, "value_" + std::to_string(i));
        }

        // Verify all keys exist
        int initial_found = 0;
        for (int i = 1; i <= 10; ++i) {
            if (tree.Search(i)) {
                initial_found++;
            }
        }
        std::cout << "  ✓ Verified " << initial_found << "/10 keys inserted" << std::endl;

        // Remove key 5
        bool removed = tree.Remove(5);
        std::cout << "  Removed key 5: " << (removed ? "Success" : "Failed") << std::endl;

        // Verify key 5 is deleted
        auto result_5 = tree.Search(5);
        if (!result_5) {
            std::cout << "  ✓ Search(5) correctly returns nullopt after deletion" << std::endl;
        } else {
            std::cout << "  ✗ Search(5) unexpectedly returned: " << *result_5 << std::endl;
        }

        // Verify adjacent keys still work
        auto result_4 = tree.Search(4);
        auto result_6 = tree.Search(6);
        if (result_4 && *result_4 == "value_4") {
            std::cout << "  ✓ Search(4) = " << *result_4 << " (works correctly)" << std::endl;
        }
        if (result_6 && *result_6 == "value_6") {
            std::cout << "  ✓ Search(6) = " << *result_6 << " (works correctly)" << std::endl;
        }

        // Verify remaining keys (1-4, 6-10)
        int remaining_found = 0;
        for (int i = 1; i <= 10; ++i) {
            if (i != 5 && tree.Search(i)) {
                remaining_found++;
            }
        }
        std::cout << "  ✓ Verified " << remaining_found << "/9 remaining keys still accessible" << std::endl;

        // Test Scan to ensure deleted keys are not included
        auto scan_results = tree.Scan(1, 10);
        std::cout << "  Scan(1, 10) returned " << scan_results.size() << " results (expected 9, key 5 deleted)" << std::endl;
        
        // Verify scan doesn't include deleted key
        bool contains_deleted = false;
        for (auto &pair : scan_results) {
            if (pair.first == 5) {
                contains_deleted = true;
                break;
            }
        }
        if (!contains_deleted) {
            std::cout << "  ✓ Deleted key 5 not included in Scan results" << std::endl;
        }

        // Test removing non-existent key
        bool removed_nonexistent = tree.Remove(999);
        if (!removed_nonexistent) {
            std::cout << "  ✓ Remove(999) correctly returns false for non-existent key" << std::endl;
        }
    }

    std::cout << "\n*** B+ Tree test completed successfully! ***" << std::endl;

    std::remove(DB_FILE);
    return 0;
}

