#include "../basic/pqxx_cp.h"
#include "../basic/utils.h"
#include "../basic/auth.h"
#include "../basic/config.h"
#include "transactions.h"
#include "actives.h"
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <chrono>

namespace actives::deals {
    class DOM {
        private:
            // contains {price, bid}
            std::map<int, utils::LinkedList> bids;
            std::shared_ptr<cp::ConnectionsManager> pool_ptr;
            void resolve_bids(std::shared_ptr<cp::ConnectionsManager> pool_ptr);
            void resolve_bids(std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

        public:
            DOM(std::shared_ptr<cp::ConnectionsManager> pool_ptr);

            bool add_bid(std::shared_ptr<cp::ConnectionsManager> pool_ptr, int bid_id, std::string user_name, int price, int amount, actives::active_obj act);

            void remove_bid(std::shared_ptr<cp::ConnectionsManager> pool_ptr, int, int);

            void remove_bid(std::shared_ptr<cp::ConnectionsManager> pool_ptr, bid);

            std::vector<bid> users_bids(std::string user_name);

            void start_resolver();

            std::vector<bid> check_bids();
    };
}

namespace actives::deals::server {
    void add_bid(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<actives::deals::DOM> dom_ptr);

    void remove_bid(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<actives::deals::DOM> dom_ptr);

    void users_bids(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<actives::deals::DOM> dom_ptr);

    void enable_bids(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
}