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
  FileCache(std::size_t max_files);
  std::shared_ptr<FileContent> get_file(const std::string &path);

private:
  std::size_t m_max_files;
  std::mutex m_mutex;
  std::unordered_map<std::string, std::shared_ptr<FileContent>> m_cache;
  std::unordered_map<std::string, std::size_t> m_usage_count;

  std::shared_ptr<FileContent> load_file(const std::string &path);
  void evict_least_popular_if_needed(const std::string &incoming_path);
};
} // namespace media_cash
