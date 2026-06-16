#include <fmt/format.h>
#include <restinio/all.hpp>

#include "./basic/auth.h"
#include "./basic/checks.h"
#include "./basic/config.h"
#include "./basic/server_traits.h"

namespace rr = restinio::router;
using router_t = rr::express_router_t<>;

std::unique_ptr<router_t>
create_registration_router(
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  auto router = std::make_unique<router_t>();
  auth::server::enable_reg(router, pool_ptr, logger_ptr);
  return router;
}

int main() {
  auto logger_ptr = std::make_shared<restinio::shared_ostream_logger_t>();

  std::shared_ptr<cp::ConnectionsManager> pool_ptr =
      std::make_shared<cp::ConnectionsManager>(logger_ptr,
                                               Config::giveMe().sql_config);

  pool_ptr->connect();
  pre_run_checks::do_checks(pool_ptr);

  auto router = create_registration_router(pool_ptr, logger_ptr);

  logger_ptr->info([] {
    return fmt::format("Registration server is starting at {}:{}",
                       Config::giveMe().server_config.adress,
                       Config::giveMe().server_config.port);
  });

  restinio::run(restinio::on_thread_pool<ws::server_traits>(
                    Config::giveMe().server_config.thread_pool_size)
                    .address(Config::giveMe().server_config.adress)
                    .port(Config::giveMe().server_config.port)
                    .request_handler(std::move(router)));
  return 0;
}
