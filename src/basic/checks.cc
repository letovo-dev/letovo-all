#include "checks.h"
namespace pre_run_checks {
void print(std::string message, int color) {
  std::cout << "\033[" << color << "m";
  std::cout << message << std::endl;
  std::cout << "\033[0m";
}

void check_departments(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  bool failed = false;
  pqxx::result all_deps = user::all_departments(pool_ptr);
  if (all_deps.size() == 0) {
    pre_run_checks::print("no deps found!", 91);
    failed = true;
  } else
    std::cout << "got " << all_deps.size() << " departments\n";
  for (auto row : all_deps) {
    std::string dep_name = row["departmentname"].as<std::string>();
    int dep_id = row["departmentid"].as<int>();
    int starter_role = user::starter_role(dep_id, pool_ptr);
    if (starter_role == -1) {
      pre_run_checks::print(
          "starter role not found for department '" + dep_name + '\'', 91);
      failed = true;
    }
  }
  if (failed) {
    throw std::runtime_error("starter role not found for some departments");
  } else {
    pre_run_checks::print("All departments have starter roles", 32);
  }
}

void do_checks(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  std::cout << std::endl << "performing pre-run checks" << std::endl;
  check_departments(pool_ptr);
  pre_run_checks::print("All checks passed", 32);
}
} // namespace pre_run_checks
