#pragma once
#include "pqxx_cp.h"
#include <pqxx/pqxx>
#include "user_data.h"

namespace pre_run_checks {
    void print(std::string message, int color);

    void check_departments(std::shared_ptr<cp::connection_pool> pool_ptr);

    void do_checks(std::shared_ptr<cp::connection_pool> pool_ptr);
}