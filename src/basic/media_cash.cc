#include "media_cash.h"
#include <iostream>

namespace media_cash {

FileCache::FileCache(std::size_t max_files, std::size_t max_bytes,
                     std::size_t max_single_file_bytes)
    : m_max_files(max_files), m_max_bytes(max_bytes),
      m_max_single_file_bytes(max_single_file_bytes) {}

std::shared_ptr<FileContent> FileCache::get_file(const std::string &path) {
  std::lock_guard<std::mutex> lock(m_mutex);

  ++m_usage_count[path];

  auto it = m_cache.find(path);
  if (it != m_cache.end()) {
    return it->second;
  }

  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  if (ec || size > m_max_single_file_bytes || size > m_max_bytes) {
    return nullptr;
  }

  auto content = load_file(path, size);
  if (content == nullptr) {
    return nullptr;
  }

  evict_until_fits(path, content->size());
  if (m_cache.size() < m_max_files && m_current_bytes + content->size() <= m_max_bytes) {
    m_current_bytes += content->size();
    m_cache[path] = content;
  }

  return content;
}

std::size_t FileCache::cached_bytes() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_current_bytes;
}

std::shared_ptr<FileContent> FileCache::load_file(const std::string &path,
                                                  std::uintmax_t size) {
  if (size > m_max_single_file_bytes) {
    return nullptr;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return nullptr;
  }

  FileContent vec(std::istreambuf_iterator<char>(file), {});
  return std::make_shared<FileContent>(std::move(vec));
}

void FileCache::evict_until_fits(const std::string &incoming_path,
                                 std::size_t incoming_size) {
  while (!m_cache.empty() &&
         (m_cache.size() >= m_max_files || m_current_bytes + incoming_size > m_max_bytes)) {
    auto min_it = std::min_element(
        m_cache.begin(), m_cache.end(), [this](const auto &a, const auto &b) {
          return m_usage_count[a.first] < m_usage_count[b.first];
        });

    if (min_it == m_cache.end()) {
      return;
    }
    if (m_usage_count[incoming_path] < m_usage_count[min_it->first] &&
        m_current_bytes + incoming_size <= m_max_bytes) {
      return;
    }

    m_current_bytes -= min_it->second->size();
    m_cache.erase(min_it);
  }
}

} // namespace media_cash
