#include "disk_manager.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

DiskManager::DiskManager(const std::string &db_file) : db_file_(db_file), num_pages_(0) {
    fd_ = open(db_file.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd_ < 0) {
        throw std::runtime_error("Failed to open database file: " + db_file);
    }

    struct stat file_stat;
    if (fstat(fd_, &file_stat) == 0) {
        num_pages_ = static_cast<int>(file_stat.st_size / PAGE_SIZE);
    }
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

void DiskManager::ReadPage(int page_id, char *page_data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    if (lseek(fd_, offset, SEEK_SET) < 0) {
        throw std::runtime_error("Failed to seek to page " + std::to_string(page_id));
    }

    ssize_t bytes_read = read(fd_, page_data, PAGE_SIZE);
    if (bytes_read < 0) {
        throw std::runtime_error("Failed to read page " + std::to_string(page_id));
    }

    if (bytes_read < static_cast<ssize_t>(PAGE_SIZE)) {
        std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
}

void DiskManager::WritePage(int page_id, const char *page_data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    if (lseek(fd_, offset, SEEK_SET) < 0) {
        throw std::runtime_error("Failed to seek to page " + std::to_string(page_id));
    }

    ssize_t bytes_written = write(fd_, page_data, PAGE_SIZE);
    if (bytes_written != static_cast<ssize_t>(PAGE_SIZE)) {
        throw std::runtime_error("Failed to write page " + std::to_string(page_id));
    }
}

int DiskManager::AllocatePage() {
    return num_pages_++;
}

int DiskManager::GetNumPages() const {
    return num_pages_;
}
