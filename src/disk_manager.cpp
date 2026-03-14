#include "disk_manager.h"

#include <cerrno>
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
    if (page_id < 0) {
        throw std::runtime_error("Invalid page id: " + std::to_string(page_id));
    }

    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    if (lseek(fd_, offset, SEEK_SET) < 0) {
        throw std::runtime_error("Failed to seek to page " + std::to_string(page_id));
    }

    // Handle partial reads and EINTR by retrying in a loop
    size_t bytes_remaining = PAGE_SIZE;
    char *dest = page_data;
    while (bytes_remaining > 0) {
        ssize_t bytes_read = read(fd_, dest, bytes_remaining);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;  // Retry on interrupt
            }
            throw std::runtime_error("Failed to read page " + std::to_string(page_id));
        }
        if (bytes_read == 0) {
            // EOF reached: zero-fill the rest of the page
            std::memset(dest, 0, bytes_remaining);
            break;
        }
        dest += bytes_read;
        bytes_remaining -= static_cast<size_t>(bytes_read);
    }
}

void DiskManager::WritePage(int page_id, const char *page_data) {
    if (page_id < 0) {
        throw std::runtime_error("Invalid page id: " + std::to_string(page_id));
    }

    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    if (lseek(fd_, offset, SEEK_SET) < 0) {
        throw std::runtime_error("Failed to seek to page " + std::to_string(page_id));
    }

    // Handle partial writes and EINTR by retrying in a loop
    size_t bytes_remaining = PAGE_SIZE;
    const char *src = page_data;
    while (bytes_remaining > 0) {
        ssize_t bytes_written = write(fd_, src, bytes_remaining);
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;  // Retry on interrupt
            }
            throw std::runtime_error("Failed to write page " + std::to_string(page_id));
        }
        src += bytes_written;
        bytes_remaining -= static_cast<size_t>(bytes_written);
    }
}

int DiskManager::AllocatePage() {
    return num_pages_++;
}

int DiskManager::GetNumPages() const {
    return num_pages_;
}
