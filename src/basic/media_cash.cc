#include "media_cash.h"
#include <iostream>
namespace media_cash {
FileCache::FileCache(std::size_t max_files) : m_max_files(max_files) {}

std::shared_ptr<FileContent> FileCache::get_file(const std::string &path) {
  std::lock_guard<std::mutex> lock(m_mutex);

  ++m_usage_count[path];

  auto it = m_cache.find(path);
  if (it != m_cache.end())
    return it->second;

  auto content = load_file(path);

  if (m_cache.size() < m_max_files) {
    m_cache[path] = content;
  } else {
    evict_least_popular_if_needed(path);
    if (m_cache.size() < m_max_files)
      m_cache[path] = content;
  }

  return content;
}

std::shared_ptr<FileContent> FileCache::load_file(const std::string &path) {
  const std::uintmax_t MAX_CACHEABLE_SIZE = 1ull << 30; // 1GB

  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  if (ec || size > MAX_CACHEABLE_SIZE)
    return nullptr;

  std::ifstream file(path, std::ios::binary);
  if (!file)
    return nullptr;

  FileContent vec(std::istreambuf_iterator<char>(file), {});
  return std::make_shared<FileContent>(std::move(vec));
}

void FileCache::evict_least_popular_if_needed(
    const std::string &incoming_path) {
  auto min_it = std::min_element(
      m_cache.begin(), m_cache.end(), [this](const auto &a, const auto &b) {
        return m_usage_count[a.first] < m_usage_count[b.first];
      });

  if (min_it != m_cache.end()) {
    if (m_usage_count[incoming_path] >= m_usage_count[min_it->first]) {
      m_cache.erase(min_it);
    }
  }
}
} // namespace media_cash