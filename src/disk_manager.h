#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "config.h"
#include <string>

class DiskManager {
public:
    explicit DiskManager(const std::string &db_file);
    ~DiskManager();

    void ReadPage(int page_id, char *page_data);
    void WritePage(int page_id, const char *page_data);

    int AllocatePage();
    int GetNumPages() const;

private:
    std::string db_file_;
    int fd_;
    int num_pages_;
};

#endif // DISK_MANAGER_H
