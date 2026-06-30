#include "./transactions.h"

namespace transactions {

    RegisteredTransaction::RegisteredTransaction() {
        reigstered_transactions = {};
    }

    RegisteredTransaction::~RegisteredTransaction() {
        reigstered_transactions.clear();
    }

    TransactionStatus RegisteredTransaction::add_transaction(std::string tr_id, std::shared_ptr<TransactionDetails> tr) {
        std::lock_guard<std::mutex> lock(mtx);
        try {
            if(reigstered_transactions.find(tr_id) != reigstered_transactions.end()) {
                return TransactionStatus::Error;
            }
            reigstered_transactions[tr_id] = tr;
            return TransactionStatus::Success;
        } catch (...) {
            return TransactionStatus::Error;
        }

    }

    TransactionStatus RegisteredTransaction::remove_transaction(std::string tr_id) {
        std::lock_guard<std::mutex> lock(mtx);
        try {
            reigstered_transactions.erase(tr_id);
        } catch (...) {
            return TransactionStatus::Error;
        }
        return TransactionStatus::Success;
    }

    std::shared_ptr<TransactionDetails> RegisteredTransaction::get_transaction(std::string tr_id) {
        std::lock_guard<std::mutex> lock(mtx);
        if(reigstered_transactions.find(tr_id) == reigstered_transactions.end()) {
            return nullptr;
        }
        return reigstered_transactions[tr_id];
    }

    int RegisteredTransaction::size() {
        std::lock_guard<std::mutex> lock(mtx);
        return reigstered_transactions.size();
    }

    RegisteredTransaction registered_transactions = RegisteredTransaction();

    int get_balance(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());

        std::vector<std::string> params = {username};

        pqxx::result result = con->execute_params("SELECT balance FROM \"user\" WHERE username=($1);", params);

        pool_ptr->returnConnection(std::move(con));

        if (result.empty()) {
            return -1;
        }
        return result[0]["balance"].as<int>();
    }


    TransactionStatus transfer(std::string tr_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto transaction = registered_transactions.get_transaction(tr_id);
        if(transaction == nullptr) {
            return TransactionStatus::WrongId;
        }
        int amount = std::stoi(transaction->amount);
        int balance = get_balance(transaction->sender, pool_ptr);
        bool sender_is_admin = auth::is_rights_by_username(transaction->sender, pool_ptr);
        if (!can_transfer_amount(balance, amount, sender_is_admin)) {
            return TransactionStatus::NoMoney;
        }

        auto con = std::move(pool_ptr->getConnection());
        std::vector<cp::Request> sql_transactions = {

            cp::Request("UPDATE \"user\" SET balance=balance-($1) WHERE username=($2);", {transaction->amount, transaction->sender}, true),
            cp::Request("UPDATE \"user\" SET balance=balance+($1) WHERE username=($2);", {transaction->amount, transaction->receiver}, true),
            cp::Request(
                "INSERT INTO \"transactions\" (sender, receiver, amount) VALUES($1, $2, $3);",
                {
                    transaction->sender,
                    transaction->receiver,
                    transaction->amount
                },
                true
            ),
            cp::Request(
                "SELECT transactionid FROM \"transactions\" WHERE sender=$1 AND receiver=$2 AND amount=$3 ORDER BY transactionid DESC LIMIT 1;",
                {
                    transaction->sender,
                    transaction->receiver,
                    transaction->amount
                }
            )
        };
        
        auto r = con -> execute_many(sql_transactions);

        pool_ptr->returnConnection(std::move(con));
        registered_transactions.remove_transaction(tr_id);

        if(r.empty()) {
            return TransactionStatus::Error;
        }


        return TransactionStatus::Success;
    }

    pqxx::result get_transactions(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params("SELECT * FROM \"transactions\" WHERE sender=($1) OR receiver=($1);", params);
        pool_ptr->returnConnection(std::move(con));
        if (result.empty()) {
            return {};
        }
        return result;
    }

    std::string last_incoming_outgoing_payments_json(
        std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params(
            R"(SELECT json_build_object(
                'last_incoming_payment',
                (
                    SELECT to_json(t)
                    FROM "transactions" t
                    WHERE t.receiver = $1
                    ORDER BY t.transactionid DESC
                    LIMIT 1
                ),
                'last_outgoing_payment',
                (
                    SELECT to_json(t)
                    FROM "transactions" t
                    WHERE t.sender = $1
                    ORDER BY t.transactionid DESC
                    LIMIT 1
                )
            )::text)",
            params);
        pool_ptr->returnConnection(std::move(con));
        if (result.empty() || result[0][0].is_null()) {
            return R"({"last_incoming_payment":null,"last_outgoing_payment":null})";
        }
        return result[0][0].as<std::string>();
    }

    std::pair<TransactionStatus, std::string> prepare_transaction(std::string sender, std::string reciver, int ammount, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        int balance = get_balance(sender, pool_ptr);
        bool sender_is_admin = auth::is_rights_by_username(sender, pool_ptr);
        if (!sender_is_admin && ammount < 0) {
            return {TransactionStatus::NegativeNumber, ""};
        } 
        if (!can_transfer_amount(balance, ammount, sender_is_admin)) {
            return {TransactionStatus::NoMoney, ""};
        }
        std::string tr_id = to_string(
                chrono::duration_cast<chrono::seconds>(chrono::system_clock::now()
                .time_since_epoch())
                .count()
            )
            + sender
            + reciver
            + to_string(ammount)
            + std::to_string(rand() % 10000);

        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {reciver};
        auto r = con -> execute_params("select * from \"user\" where username=($1);", params, true);
        pool_ptr->returnConnection(std::move(con));
        if (r.empty()) {
            return {TransactionStatus::WrongId, ""};
        }
        
        return {registered_transactions.add_transaction(tr_id, std::make_shared<TransactionDetails>(sender, reciver, std::to_string(ammount))), tr_id};
    }
} // namespace transactions

namespace transactions::server {
    void prepare_transaction(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/transactions/prepare", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /transactions/prepare";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            if (!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            if (new_body.HasMember("receiver") && new_body.HasMember("amount")) {
                std::string sender = auth::get_username(token, pool_ptr);
                std::string receiver = new_body["receiver"].GetString();
                if(auth::is_user(receiver, pool_ptr) == false) {
                    return req->create_response(restinio::status_not_acceptable())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("receiver not found")
                    .done();
                }
                if (!new_body["amount"].IsInt()) {
                    return req->create_response(restinio::status_bad_request())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("amount must be an integer")
                    .done();
                }
                int amount = new_body["amount"].GetInt();
                auto tr_id = transactions::prepare_transaction(sender, receiver, amount, pool_ptr);
                switch (tr_id.first)
                {
                case TransactionStatus::Success:
                    return req->create_response()
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body(tr_id.second)
                    .done();    
                    break;
                case TransactionStatus::NoMoney:
                    return req->create_response(restinio::status_conflict())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body(Comment::giveMe().no_money)
                    .done();
                    break;
                case TransactionStatus::WrongId:
                    return req->create_response(restinio::status_not_found())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("wrong username")
                    .done();
                    break;
                case TransactionStatus::Error:
                default:
                    break;
                }
            } 
            return req->create_response(
                restinio::status_non_authoritative_information()
            )
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("receiver: " + std::to_string(new_body.HasMember("receiver")) + " amount: " + std::to_string(new_body.HasMember("amount")))
            .done();
        
        });
    }


    void transfer(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/transactions/send", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /transactions/send";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().c_str());

            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            if (new_body.HasMember("tr_id")) {
                std::string tr_id = new_body["tr_id"].GetString();
                logger_ptr->info([token, tr_id] { return fmt::format("token = {}, tr_id = {}", token, tr_id); });
                switch (transactions::transfer(tr_id, pool_ptr))
                {
                case TransactionStatus::Success:
                    return req->create_response()
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("ok")
                    .done();
                    break;
                case TransactionStatus::NoMoney:
                    return req->create_response(restinio::status_conflict())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body(Comment::giveMe().no_money)
                    .done();
                    break;
                case TransactionStatus::WrongId:
                    return req->create_response(restinio::status_not_acceptable())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("wrong id")
                    .done();
                    break;
                case TransactionStatus::Error:
                default:
                    return req->create_response(restinio::status_internal_server_error())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("internal server error")
                    .done();
                }
            } else {
                return req->create_response(restinio::status_non_authoritative_information())
                .done();
            }
        });
    }

    void get_balance(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/transactions/balance", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /transactions/balance";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            if (!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            std::string username = auth::get_username(token, pool_ptr);

            return req->create_response()
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body(std::to_string(transactions::get_balance(username, pool_ptr)))
                .done();
        });
    }

    void get_transactions(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/transactions/my", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /transactions/my";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            if (!auth::is_authed(token, pool_ptr)) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }

            std::string username = auth::get_username(token, pool_ptr);

            return req->create_response()
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(cp::serialize_with_shift_day(transactions::get_transactions(username, pool_ptr), pool_ptr))
                .done();
        });
    }
} // namespace transactions::server
