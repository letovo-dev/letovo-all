#pragma once
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <jwt-cpp/jwt.h>
#include <restinio/all.hpp>
#include <pqxx/pqxx>
#include <unordered_set>
// #include "spdlog/spdlog.h"
#include "rapidjson/document.h"
#include "asio/ip/detail/endpoint.hpp"
#include "../basic/pqxx_cp.h"
#include "../basic/hash.h"
#include "../basic/auth.h"
#include "../basic/url_parser.h"
#include "../basic/comment.h"
#include "../basic/ws_event_bus.h"
#include "transaction_rules.h"
#include <vector>
#include <cstdlib>

namespace transactions {
    struct TransactionDetails {
        std::string sender;
        std::string receiver;
        std::string amount;
    };

    enum struct TransactionStatus {
        NoMoney,
        WrongId,
        Error,
        Success,
        NegativeNumber,
        NotReceiver,
        InactiveUser,
    };

    struct TransferResult {
        TransactionStatus status = TransactionStatus::Error;
        std::string sender;
        std::string receiver;
        int amount = 0;
        int transaction_id = 0;
        int sender_balance = 0;
        int receiver_balance = 0;
    };
    
    class RegisteredTransaction {
        public:
            RegisteredTransaction();
            ~RegisteredTransaction();
            TransactionStatus add_transaction(std::string tr_id, std::shared_ptr<TransactionDetails> tr);
            TransactionStatus remove_transaction(std::string tr_id);
            std::shared_ptr<TransactionDetails> get_transaction(std::string tr_id);
            int size();
        private:
            std::unordered_map<std::string, std::shared_ptr<TransactionDetails>> reigstered_transactions;
            std::mutex mtx;
    };
    
    TransactionStatus transfer(std::string tr_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    TransferResult transfer_with_result(std::string tr_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    int get_balance(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool can_use_transactions(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool can_receive_transfer(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    bool has_whireable_participant(std::string sender, std::string receiver, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    pqxx::result get_transactions(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    /** JSON object: last_incoming_payment, last_outgoing_payment — each is a full transaction row or null. */
    std::string last_incoming_outgoing_payments_json(
        std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    std::pair<TransactionStatus, std::string> prepare_transaction(std::string sender, std::string reciver, int ammount, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
} // namespace transactions

namespace transactions::server {
    void transfer(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<ws::EventBus> bus_ptr);

    void get_balance(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void get_transactions(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);

    void prepare_transaction(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
} // namespace transactions::server
