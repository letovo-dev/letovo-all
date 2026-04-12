#include "pqxx_cp.h"

#include <cstdio>
#include <ctime>
#include <string_view>

namespace {

std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20U) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out;
}

struct CalendarSegment {
    std::string chapter;
    time_t start;
    time_t end;
};

// Parse PostgreSQL timestamp "YYYY-MM-DD HH:MM:SS"
time_t parse_pg_timestamp(const std::string &s) {
    struct tm t = {};
    std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d",
                &t.tm_year, &t.tm_mon, &t.tm_mday,
                &t.tm_hour, &t.tm_min, &t.tm_sec);
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    return std::mktime(&t);
}

struct SegmentInfo {
    int day = -1;       // 1-based, -1 means not found
    std::string chapter;
};

// Returns 1-based day and chapter name, or day=-1 if datetime is outside all segments
SegmentInfo compute_segment_info(time_t dt, const std::vector<CalendarSegment> &calendar) {
    for (const auto &seg : calendar) {
        if (dt >= seg.start && dt <= seg.end) {
            struct tm dt_tm, seg_tm;
            localtime_r(&dt, &dt_tm);
            localtime_r(&seg.start, &seg_tm);
            dt_tm.tm_hour = dt_tm.tm_min = dt_tm.tm_sec = 0;
            seg_tm.tm_hour = seg_tm.tm_min = seg_tm.tm_sec = 0;
            time_t dt_day = std::mktime(&dt_tm);
            time_t seg_day = std::mktime(&seg_tm);
            return {static_cast<int>((dt_day - seg_day) / 86400) + 1, seg.chapter};
        }
    }
    return {};
}

} // namespace

namespace cp {
std::string serialize(const pqxx::result &res) {
  // TODO: it can be better
  if (res.empty()) {
    return "{\"result\": []}";
  }
  std::string res_str = "{\"result\": [";
  for (auto const &row : res) {
    res_str += cp::serialize(row);
    res_str += ',';
  }
  res_str[res_str.length() - 1] = ']';
  res_str += "}";
  return res_str;
}

std::string serialize(const pqxx::row &row) {
  // TODO: it can be better
  if (row.empty()) {
    return "{}";
  }
  std::string res_str = "{";
  for (auto const &field : row) {
    res_str += '"';
    res_str += json_escape(field.name());
    res_str += "\": ";
    if (field.is_null()) {
      res_str += "null";
    } else {
      res_str += '"';
      res_str += json_escape(field.c_str());
      res_str += '"';
    }
    res_str += ',';
  }

  res_str[res_str.length() - 1] = '}';
  return res_str;
}

std::string serialize(const std::vector<std::string> &vec) {
  if (vec.empty()) {
    return "{}";
  }
  std::string res_str = "{\"result\": [";
  for (auto const &field : vec) {
    res_str += '"';
    res_str += json_escape(field);
    res_str += "\",";
  }
  res_str[res_str.length() - 1] = ']';
  res_str += "}";
  return res_str;
}

std::string serialize_with_segment_day(const pqxx::result &res, std::shared_ptr<ConnectionsManager> pool_ptr) {
  if (res.empty()) {
    return "{\"result\": []}";
  }

  // Find 'datetime' or 'date' column index
  int datetime_col = -1;
  for (int i = 0; i < static_cast<int>(res.columns()); ++i) {
    std::string col(res.column_name(i));
    if (col == "datetime" || col == "date") {
      datetime_col = i;
      break;
    }
  }

  if (datetime_col == -1) {
    return serialize(res);
  }

  // Fetch all calendar segments once
  std::vector<CalendarSegment> calendar;
  try {
    auto con = std::move(pool_ptr->getConnection());
    pqxx::result cal = con->execute(
        R"(SELECT chapter, start, "end" FROM "calendar" ORDER BY start)");
    pool_ptr->returnConnection(std::move(con));

    for (const auto &row : cal) {
      CalendarSegment seg;
      seg.chapter = row["chapter"].as<std::string>();
      seg.start = parse_pg_timestamp(row["start"].as<std::string>());
      seg.end = parse_pg_timestamp(row["end"].as<std::string>());
      calendar.push_back(std::move(seg));
    }
  } catch (...) {
    return serialize(res);
  }

  std::string res_str = "{\"result\": [";
  for (const auto &row : res) {
    std::string row_str = "{";
    for (const auto &field : row) {
      row_str += '"';
      row_str += json_escape(field.name());
      row_str += "\": ";
      if (field.is_null()) {
        row_str += "null";
      } else {
        row_str += '"';
        row_str += json_escape(field.c_str());
        row_str += '"';
      }
      row_str += ',';
    }

    if (!row[datetime_col].is_null()) {
      SegmentInfo info = compute_segment_info(
          parse_pg_timestamp(row[datetime_col].as<std::string>()), calendar);
      if (info.day > 0) {
        row_str += "\"segment_day\": ";
        row_str += std::to_string(info.day);
        row_str += ", \"segment_chapter\": \"";
        row_str += json_escape(info.chapter);
        row_str += "\",";
      }
    }

    row_str[row_str.length() - 1] = '}';
    res_str += row_str;
    res_str += ',';
  }
  res_str[res_str.length() - 1] = ']';
  res_str += "}";
  return res_str;
}

AsyncConnection::AsyncConnection(
    const connection_options &options,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    std::string name)
    : name(name), logger_ptr_(logger_ptr) {
  connect_string = "dbname = " + options.dbname + " user = " + options.user +
                   " password = " + options.password +
                   " hostaddr = " + options.hostaddr +
                   " port = " + options.port;
  con = std::make_shared<pqxx::connection>(connect_string);
  last_used = std::chrono::system_clock::now();
}
AsyncConnection::AsyncConnection(const AsyncConnection &db) {
  connect_string = db.connect_string;
  con = db.con;
  name = db.name;
  last_used = std::chrono::system_clock::now();
  logger_ptr_ = db.logger_ptr_;
}
pqxx::result AsyncConnection::query(const std::string &sql) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r = w.exec(sql);
  last_used = std::chrono::system_clock::now();
  return r;
}
void AsyncConnection::prepare(const std::string &sql) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  con->prepare(name, sql);
  last_used = std::chrono::system_clock::now();
}

void AsyncConnection::prepare(const std::string &_name,
                              const std::string &sql) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  con->prepare(_name, sql);
  last_used = std::chrono::system_clock::now();
}

pqxx::result AsyncConnection::execute_prepared(int &&args) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r = w.exec_prepared(name, args);
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_prepared(const std::string &&args) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r = w.exec_prepared(name, args);
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_prepared(const std::string &_name,
                                               int &&args) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r = w.exec_prepared(name, args);
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_prepared(const std::string &_name,
                                               std::basic_string<char> &args) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r = w.exec_prepared(name, args);
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_params(const std::string &sql,
                                             std::vector<std::string> &params,
                                             bool commit) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r;
  try {
    r = w.exec_params(sql, pqxx::prepare::make_dynamic_params(params));
  } catch (const std::exception &e) {
    logger_ptr_->error(
        [e, sql] { return fmt::format("{} in {}", e.what(), sql); });
    logger_ptr_->error(
        [params] { return fmt::format("Params: {}", cp::serialize(params)); });
  }
  if (commit) {
    w.commit();
  }
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_params(const std::string &sql,
                                             std::vector<int> &params,
                                             bool commit) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r;
  try {
    r = w.exec_params(sql, pqxx::prepare::make_dynamic_params(params));
  } catch (const std::exception &e) {
    logger_ptr_->error(
        [e, sql] { return fmt::format("Error: {} in {}", e.what(), sql); });
  }
  if (commit) {
    w.commit();
  }
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_params(Request req) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r;
  try {
    r = w.exec_params(req.sql, pqxx::prepare::make_dynamic_params(req.params));
  } catch (const std::exception &e) {
    logger_ptr_->error(
        [e, req] { return fmt::format("Error: {} in {}", e.what(), req.sql); });
  }
  if (req.commit) {
    w.commit();
  }
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute_many(std::vector<Request> &reqs) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  bool commit = false;
  pqxx::work w(*con);
  pqxx::result r;
  for (auto &req : reqs) {
    try {
      r = w.exec_params(req.sql,
                        pqxx::prepare::make_dynamic_params(req.params));
    } catch (const std::exception &e) {
      logger_ptr_->error([e, req] {
        return fmt::format("Error: {} in {}", e.what(), req.sql);
      });
    }
    if (req.commit) {
      commit = true;
    }
  }
  if (commit)
    w.commit();
  last_used = std::chrono::system_clock::now();
  return r;
}

pqxx::result AsyncConnection::execute(const std::string &sql, bool commit) {
  if (!is_open()) {
    con = std::make_shared<pqxx::connection>(connect_string);
  }
  pqxx::work w(*con);
  pqxx::result r;
  try {
    r = w.exec(sql);
  } catch (const std::exception &e) {
    logger_ptr_->error(
        [e, sql] { return fmt::format("Error: {} in {}", e.what(), sql); });
  }
  if (commit) {
    w.commit();
  }
  last_used = std::chrono::system_clock::now();
  return r;
}

bool AsyncConnection::is_open() {
  return last_used + std::chrono::seconds(60) >
         std::chrono::system_clock::now();
}

ConnectionsManager::ConnectionsManager(
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    const connection_options &options)
    : options(options), logger_ptr_(logger_ptr),
      numberOfConnections(options.connections_count) {}

ConnectionsManager::ConnectionsManager(
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr,
    const connection_options &options, int numberOfConnections)
    : options(options), logger_ptr_(logger_ptr),
      numberOfConnections(numberOfConnections) {}

ConnectionsManager::ConnectionsManager() {}

void ConnectionsManager::connect() {
  for (int i = 0; i < numberOfConnections; i++) {
    std::unique_ptr<AsyncConnection> db_ptr = std::make_unique<AsyncConnection>(
        options, logger_ptr_, std::to_string(i));
    connections.push(std::move(db_ptr));
  }
}

std::unique_ptr<AsyncConnection> ConnectionsManager::getConnection() {
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [this] { return !connections.empty(); });
  if (!connections.empty()) {
    // std::cout << "Database available" << std::endl;
    std::unique_ptr<AsyncConnection> db_ptr = std::move(connections.front());
    connections.pop();
    lock.unlock();
    cv.notify_one();
    return db_ptr;
  }
  if (connections.empty()) {
    throw std::runtime_error("queue empty");
  }
  throw std::runtime_error("No database available");
}

void ConnectionsManager::returnConnection(
    std::unique_ptr<AsyncConnection> db_ptr) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    connections.push(std::move(db_ptr));
  }
  cv.notify_one();
}

SafeCon::SafeCon(std::shared_ptr<ConnectionsManager> pool_ptr)
    : std::unique_ptr<AsyncConnection>(std::move(pool_ptr->getConnection())),
      pool_ptr_(pool_ptr) {}

SafeCon::~SafeCon() {
  pool_ptr_->returnConnection(std::move(*this));
}

} // namespace cp
