#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace media_cash {
using FileContent = std::vector<uint8_t>;
class FileCache {
public:
  explicit FileCache(std::size_t max_files,
                     std::size_t max_bytes = 128ull * 1024ull * 1024ull,
                     std::size_t max_single_file_bytes = 2ull * 1024ull * 1024ull);
  std::shared_ptr<FileContent> get_file(const std::string &path);
  std::size_t cached_bytes() const;

private:
  std::size_t m_max_files;
  std::size_t m_max_bytes;
  std::size_t m_max_single_file_bytes;
  std::size_t m_current_bytes = 0;
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, std::shared_ptr<FileContent>> m_cache;
  std::unordered_map<std::string, std::size_t> m_usage_count;

  std::shared_ptr<FileContent> load_file(const std::string &path, std::uintmax_t size);
  void evict_until_fits(const std::string &incoming_path, std::size_t incoming_size);
};
} // namespace media_cash
