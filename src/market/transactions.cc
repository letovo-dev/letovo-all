#include "./transactions.h"
#include "../basic/analytics.h"
#include <cctype>
#include <cstdint>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

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

    bool can_use_transactions(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        return auth::is_active(username, pool_ptr);
    }

    bool can_receive_transfer(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        return auth::is_rights_by_username(username, pool_ptr, "whireable");
    }

    bool has_whireable_participant(std::string sender, std::string receiver, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        const bool sender_whireable = can_receive_transfer(sender, pool_ptr);
        if (sender_whireable) {
            return true;
        }
        return has_whireable_participant(sender_whireable, can_receive_transfer(receiver, pool_ptr));
    }

    int transfer_cooldown_seconds_remaining(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username, std::to_string(kTransferCooldownSeconds)};
        pqxx::result result = con->execute_params(
            R"(SELECT CASE
                   WHEN MAX(transactiontime) IS NULL THEN 0
                   ELSE GREATEST(
                       0,
                       $2::integer - FLOOR(EXTRACT(EPOCH FROM (LOCALTIMESTAMP - MAX(transactiontime))))::integer
                   )
               END AS seconds_remaining
               FROM "transactions"
               WHERE sender = $1)",
            params);
        pool_ptr->returnConnection(std::move(con));

        if (result.empty() || result[0]["seconds_remaining"].is_null()) {
            return 0;
        }
        return result[0]["seconds_remaining"].as<int>();
    }


    TransferResult transfer_with_result(std::string tr_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        TransferResult result;
        auto transaction = registered_transactions.get_transaction(tr_id);
        if(transaction == nullptr) {
            result.status = TransactionStatus::WrongId;
            return result;
        }
        int amount = std::stoi(transaction->amount);
        result.sender = transaction->sender;
        result.receiver = transaction->receiver;
        result.amount = amount;
        bool sender_is_admin = auth::is_rights_by_username(transaction->sender, pool_ptr);

        if (transfer_cooldown_seconds_remaining(transaction->sender, pool_ptr) > 0) {
            result.status = TransactionStatus::Cooldown;
            return result;
        }

        if (!sender_is_admin &&
            (!can_use_transactions(transaction->sender, pool_ptr) ||
             !can_use_transactions(transaction->receiver, pool_ptr))) {
            result.status = TransactionStatus::InactiveUser;
            return result;
        }

        if (!sender_is_admin && !has_whireable_participant(transaction->sender, transaction->receiver, pool_ptr)) {
            result.status = TransactionStatus::NotReceiver;
            return result;
        }

        int balance = get_balance(transaction->sender, pool_ptr);
        if (!can_transfer_amount(balance, amount, sender_is_admin)) {
            result.status = TransactionStatus::NoMoney;
            return result;
        }

        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {
            transaction->amount,
            transaction->sender,
            transaction->receiver
        };
        pqxx::result r;

        if (transaction->sender == transaction->receiver) {
            r = con->execute_params(
                R"(WITH inserted_transaction AS (
                       INSERT INTO "transactions" (sender, receiver, amount)
                       VALUES ($2, $3, $1::integer)
                       RETURNING transactionid
                   )
                   SELECT
                       (SELECT balance FROM "user" WHERE username = $2) AS sender_balance,
                       (SELECT balance FROM "user" WHERE username = $2) AS receiver_balance,
                       (SELECT transactionid FROM inserted_transaction) AS transaction_id)",
                params,
                true);
        } else {
            r = con->execute_params(
                R"(WITH updated_sender AS (
                       UPDATE "user"
                       SET balance = balance - $1::integer
                       WHERE username = $2
                       RETURNING balance
                   ),
                   updated_receiver AS (
                       UPDATE "user"
                       SET balance = balance + $1::integer
                       WHERE username = $3
                       RETURNING balance
                   ),
                   inserted_transaction AS (
                       INSERT INTO "transactions" (sender, receiver, amount)
                       VALUES ($2, $3, $1::integer)
                       RETURNING transactionid
                   )
                   SELECT
                       (SELECT balance FROM updated_sender) AS sender_balance,
                       (SELECT balance FROM updated_receiver) AS receiver_balance,
                       (SELECT transactionid FROM inserted_transaction) AS transaction_id)",
                params,
                true);
        }

        pool_ptr->returnConnection(std::move(con));

        if(r.empty() ||
           r[0]["sender_balance"].is_null() ||
           r[0]["receiver_balance"].is_null() ||
           r[0]["transaction_id"].is_null()) {
            result.status = TransactionStatus::Error;
            return result;
        }

        result.sender_balance = r[0]["sender_balance"].as<int>();
        result.receiver_balance = r[0]["receiver_balance"].as<int>();
        result.transaction_id = r[0]["transaction_id"].as<int>();
        registered_transactions.remove_transaction(tr_id);
        result.status = TransactionStatus::Success;
        return result;
    }

    TransactionStatus transfer(std::string tr_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        return transfer_with_result(std::move(tr_id), std::move(pool_ptr)).status;
    }

    pqxx::result get_transactions(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params(
            "SELECT * FROM \"transactions\" "
            "WHERE (sender=($1) OR receiver=($1)) "
            "AND transactiontime >= LOCALTIMESTAMP - INTERVAL '7 days' "
            "ORDER BY transactiontime DESC, transactionid DESC "
            "LIMIT 100;",
            params);
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

    DepartmentPayoutResult preview_department_payout(
        int department_id, int amount,
        std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        DepartmentPayoutResult payout;
        payout.department_id = department_id;
        payout.amount = amount;

        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {
            std::to_string(department_id), std::to_string(amount)};
        pqxx::result rows = con->execute_params(
            R"(SELECT d.departmentid,
                      d.departmentname,
                      COUNT(u.username)::integer AS recipient_count,
                      (COUNT(u.username) * $2::bigint)::bigint AS total
               FROM "department" d
               LEFT JOIN "roles" r
                 ON r.departmentid = d.departmentid
                AND r.roleid BETWEEN 1 AND 28
               LEFT JOIN "user" u
                 ON u.role = r.roleid
                AND u.active = true
                AND u.registered = true
               WHERE d.departmentid = $1::integer
               GROUP BY d.departmentid, d.departmentname)",
            params);
        pool_ptr->returnConnection(std::move(con));

        if (rows.empty()) {
            payout.status = DepartmentPayoutStatus::DepartmentNotFound;
            return payout;
        }

        payout.department_name = rows[0]["departmentname"].as<std::string>();
        payout.recipient_count = rows[0]["recipient_count"].as<int>();
        payout.total = rows[0]["total"].as<long long>();
        payout.status = payout.recipient_count == 0
            ? DepartmentPayoutStatus::NoRecipients
            : DepartmentPayoutStatus::Success;
        return payout;
    }

    DepartmentPayoutResult apply_department_payout(
        int department_id, int amount, const std::string& actor,
        const std::string& request_id, int expected_recipient_count,
        std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        DepartmentPayoutResult payout;
        payout.department_id = department_id;
        payout.amount = amount;

        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {
            std::to_string(department_id), std::to_string(amount), actor,
            request_id, std::to_string(expected_recipient_count)};
        pqxx::result rows = con->execute_params_or_throw(
            R"(WITH request_lock AS (
                   SELECT pg_advisory_xact_lock(hashtext($4))
               ),
               department_row AS (
                   SELECT d.departmentid, d.departmentname
                   FROM "department" d
                   CROSS JOIN request_lock
                   WHERE d.departmentid = $1::integer
               ),
               recipients AS MATERIALIZED (
                   SELECT u.username
                   FROM "user" u
                   JOIN "roles" r ON r.roleid = u.role
                   CROSS JOIN department_row d
                   WHERE r.departmentid = d.departmentid
                     AND r.roleid BETWEEN 1 AND 28
                     AND u.active = true
                     AND u.registered = true
               ),
               recipient_totals AS (
                   SELECT COUNT(*)::integer AS recipient_count
                   FROM recipients
               ),
               balance_guard AS (
                   SELECT COALESCE(
                       BOOL_AND(u.balance <= 2147483647 - $2::integer), false
                   ) AS safe
                   FROM "user" u
                   JOIN recipients r ON r.username = u.username
               ),
               existing AS (
                   SELECT COUNT(*)::integer AS recipient_count,
                          COALESCE(SUM(amount), 0)::bigint AS total
                   FROM "transactions"
                   WHERE reason LIKE ('%; request ' || $4)
               ),
               updated AS (
                   UPDATE "user" u
                   SET balance = u.balance + $2::integer
                   FROM recipients r, recipient_totals totals, existing prior,
                        balance_guard guard
                   WHERE u.username = r.username
                     AND prior.recipient_count = 0
                     AND totals.recipient_count = $5::integer
                     AND guard.safe
                   RETURNING u.username
               ),
               inserted AS (
                   INSERT INTO "transactions" (sender, receiver, amount, reason)
                   SELECT $3,
                          updated.username,
                          $2::integer,
                          'department payout: ' || d.departmentname ||
                          '; issued by ' || $3 || '; request ' || $4
                   FROM updated
                   CROSS JOIN department_row d
                   RETURNING transactionid
               )
               SELECT d.departmentid,
                      d.departmentname,
                      totals.recipient_count AS eligible_count,
                      CASE WHEN prior.recipient_count > 0
                           THEN prior.recipient_count
                           ELSE totals.recipient_count END AS recipient_count,
                      CASE WHEN prior.recipient_count > 0
                           THEN prior.total
                           ELSE totals.recipient_count::bigint * $2::bigint END AS total,
                      (SELECT COUNT(*)::integer FROM inserted) AS inserted_count,
                      (prior.recipient_count > 0) AS duplicate
               FROM department_row d
               CROSS JOIN recipient_totals totals
               CROSS JOIN existing prior)",
            params, true);

        if (rows.empty()) {
            std::vector<std::string> department_params = {
                std::to_string(department_id)};
            pqxx::result department = con->execute_params_or_throw(
                R"(SELECT 1 FROM "department" WHERE departmentid = $1::integer)",
                department_params);
            payout.status = department.empty()
                ? DepartmentPayoutStatus::DepartmentNotFound
                : DepartmentPayoutStatus::Error;
            return payout;
        }

        payout.department_name = rows[0]["departmentname"].as<std::string>();
        const int eligible_count = rows[0]["eligible_count"].as<int>();
        payout.recipient_count = rows[0]["recipient_count"].as<int>();
        payout.total = rows[0]["total"].as<long long>();
        payout.duplicate = rows[0]["duplicate"].as<bool>();
        const int inserted_count = rows[0]["inserted_count"].as<int>();

        if (payout.duplicate) {
            payout.status = DepartmentPayoutStatus::Success;
            return payout;
        }
        if (eligible_count == 0) {
            payout.status = DepartmentPayoutStatus::NoRecipients;
            return payout;
        }
        if (eligible_count != expected_recipient_count) {
            payout.status = DepartmentPayoutStatus::PreviewChanged;
            return payout;
        }
        if (inserted_count != eligible_count) {
            payout.status = DepartmentPayoutStatus::Error;
            return payout;
        }

        payout.applied = true;
        payout.status = DepartmentPayoutStatus::Success;
        return payout;
    }

    std::pair<TransactionStatus, std::string> prepare_transaction(std::string sender, std::string reciver, int ammount, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        const bool sender_is_admin = auth::is_rights_by_username(sender, pool_ptr);
        if (!sender_is_admin && !can_use_transactions(sender, pool_ptr)) {
            return {TransactionStatus::InactiveUser, ""};
        }
        int balance = get_balance(sender, pool_ptr);
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
        if (!sender_is_admin && !can_use_transactions(reciver, pool_ptr)) {
            return {TransactionStatus::InactiveUser, ""};
        }
        if (!sender_is_admin && !has_whireable_participant(sender, reciver, pool_ptr)) {
            return {TransactionStatus::NotReceiver, ""};
        }
        if (transfer_cooldown_seconds_remaining(sender, pool_ptr) > 0) {
            return {TransactionStatus::Cooldown, ""};
        }
        
        return {registered_transactions.add_transaction(tr_id, std::make_shared<TransactionDetails>(sender, reciver, std::to_string(ammount))), tr_id};
    }
} // namespace transactions

namespace transactions::server {
    static const char* department_payout_status_name(
        transactions::DepartmentPayoutStatus status) {
        switch (status) {
        case transactions::DepartmentPayoutStatus::Success:
            return "success";
        case transactions::DepartmentPayoutStatus::DepartmentNotFound:
            return "department_not_found";
        case transactions::DepartmentPayoutStatus::NoRecipients:
            return "no_recipients";
        case transactions::DepartmentPayoutStatus::PreviewChanged:
            return "preview_changed";
        case transactions::DepartmentPayoutStatus::Error:
        default:
            return "error";
        }
    }

    static std::string department_payout_json(
        const transactions::DepartmentPayoutResult& payout) {
        rapidjson::Document body(rapidjson::kObjectType);
        auto& allocator = body.GetAllocator();
        body.AddMember("department_id", payout.department_id, allocator);
        body.AddMember(
            "department_name",
            rapidjson::Value(payout.department_name.c_str(), allocator), allocator);
        body.AddMember("amount", payout.amount, allocator);
        body.AddMember("recipient_count", payout.recipient_count, allocator);
        body.AddMember("total", static_cast<int64_t>(payout.total), allocator);
        body.AddMember("applied", payout.applied, allocator);
        body.AddMember("duplicate", payout.duplicate, allocator);
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        body.Accept(writer);
        return buffer.GetString();
    }

    static bool valid_payout_request_id(const std::string& request_id) {
        if (request_id.size() < 16 || request_id.size() > 64) {
            return false;
        }
        for (unsigned char c : request_id) {
            if (!std::isalnum(c) && c != '-') {
                return false;
            }
        }
        return true;
    }

    void department_payout(
        std::unique_ptr<restinio::router::express_router_t<>>& router,
        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post(
            "/transactions/department-payout",
            [pool_ptr, logger_ptr](auto req, auto) {
                logger_ptr->trace([] { return "called /transactions/department-payout"; });
                const std::string token = security::bearer_or_cookie_token(req->header());
                if (token.empty() || !auth::is_authed(token, pool_ptr)) {
                    return req->create_response(restinio::status_unauthorized()).done();
                }
                if (!auth::is_admin(token, pool_ptr)) {
                    return req->create_response(restinio::status_forbidden())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"admin rights required"})")
                        .done();
                }

                rapidjson::Document body;
                body.Parse(req->body().c_str());
                if (body.HasParseError() || !body.IsObject() ||
                    !body.HasMember("department_id") || !body["department_id"].IsInt() ||
                    !body.HasMember("amount") || !body["amount"].IsInt() ||
                    !body.HasMember("request_id") || !body["request_id"].IsString() ||
                    !body.HasMember("confirm") || !body["confirm"].IsBool()) {
                    return req->create_response(restinio::status_bad_request())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"department_id, positive integer amount, request_id and confirm are required"})")
                        .done();
                }

                const int department_id = body["department_id"].GetInt();
                const int amount = body["amount"].GetInt();
                const std::string request_id = body["request_id"].GetString();
                const bool confirm = body["confirm"].GetBool();
                if (department_id <= 0 || amount <= 0 ||
                    !valid_payout_request_id(request_id) ||
                    (confirm && (!body.HasMember("expected_recipient_count") ||
                                 !body["expected_recipient_count"].IsInt() ||
                                 body["expected_recipient_count"].GetInt() <= 0))) {
                    return req->create_response(restinio::status_bad_request())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"invalid department payout parameters"})")
                        .done();
                }

                const std::string actor = auth::get_username(token, pool_ptr);
                transactions::DepartmentPayoutResult payout;
                try {
                    payout = confirm
                        ? transactions::apply_department_payout(
                            department_id, amount, actor,
                            request_id, body["expected_recipient_count"].GetInt(), pool_ptr)
                        : transactions::preview_department_payout(department_id, amount, pool_ptr);
                } catch (const pqxx::sql_error& error) {
                    const std::string sqlstate = error.sqlstate();
                    const std::string message = error.what();
                    logger_ptr->error([sqlstate, message] {
                        return "department payout database failure: SQLSTATE=" +
                            sqlstate + " message=" + message;
                    });
                    return req->create_response(restinio::status_internal_server_error())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"department payout failed"})").done();
                } catch (const std::exception& error) {
                    const std::string message = error.what();
                    logger_ptr->error([message] {
                        return "department payout database failure: " + message;
                    });
                    return req->create_response(restinio::status_internal_server_error())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"department payout failed"})").done();
                }
                logger_ptr->info([department_id, confirm, status = payout.status] {
                    return fmt::format(
                        "department_payout department_id={} confirm={} outcome={}",
                        department_id, confirm, department_payout_status_name(status));
                });

                switch (payout.status) {
                case transactions::DepartmentPayoutStatus::Success:
                    if (confirm && payout.applied) {
                        analytics::record_event(
                            actor, token,
                            req->remote_endpoint().address().to_string(),
                            req->header().has_field("User-Agent")
                                ? req->header().get_field("User-Agent") : "",
                            "POST", "/transactions/department-payout", 200, 0,
                            "", "{}", pool_ptr, logger_ptr);
                    }
                    return req->create_response()
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(department_payout_json(payout)).done();
                case transactions::DepartmentPayoutStatus::DepartmentNotFound:
                    return req->create_response(restinio::status_not_found())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"department not found"})").done();
                case transactions::DepartmentPayoutStatus::NoRecipients:
                    return req->create_response(restinio::status_conflict())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"department has no eligible recipients"})").done();
                case transactions::DepartmentPayoutStatus::PreviewChanged:
                    return req->create_response(restinio::status_conflict())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"recipient list changed; preview again"})").done();
                case transactions::DepartmentPayoutStatus::Error:
                default:
                    return req->create_response(restinio::status_internal_server_error())
                        .append_header("Content-Type", "application/json; charset=utf-8")
                        .set_body(R"({"error":"department payout failed"})").done();
                }
            });
    }

    static void publish_balance_update(
        const std::shared_ptr<ws::EventBus>& bus_ptr,
        const std::string& username,
        int balance,
        int delta,
        const std::string& counterparty,
        const std::string& direction,
        int transaction_id) {
        if (!bus_ptr) {
            return;
        }

        const std::string topic = "inbox:" + username;
        rapidjson::Document data(rapidjson::kObjectType);
        auto& allocator = data.GetAllocator();
        data.AddMember("balance", balance, allocator);
        data.AddMember("delta", delta, allocator);
        data.AddMember("counterparty", rapidjson::Value(counterparty.c_str(), allocator), allocator);
        data.AddMember("direction", rapidjson::Value(direction.c_str(), allocator), allocator);
        data.AddMember("transaction_id", transaction_id, allocator);

        bus_ptr->publish(topic, ws::make_envelope("transaction.balance.updated", topic, data));
    }

    static void publish_transfer_result(
        const std::shared_ptr<ws::EventBus>& bus_ptr,
        const transactions::TransferResult& result) {
        if (result.sender == result.receiver) {
            publish_balance_update(
                bus_ptr,
                result.sender,
                result.sender_balance,
                0,
                result.receiver,
                "self",
                result.transaction_id);
            return;
        }

        publish_balance_update(
            bus_ptr,
            result.sender,
            result.sender_balance,
            -result.amount,
            result.receiver,
            "outgoing",
            result.transaction_id);
        publish_balance_update(
            bus_ptr,
            result.receiver,
            result.receiver_balance,
            result.amount,
            result.sender,
            "incoming",
            result.transaction_id);
    }

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
                bool sender_is_admin = auth::is_rights_by_username(sender, pool_ptr);
                if (!sender_is_admin && !transactions::can_use_transactions(sender, pool_ptr)) {
                    return req->create_response(restinio::status_forbidden())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("user is not active")
                    .done();
                }
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
                    analytics::record_event(sender, token, req->remote_endpoint().address().to_string(),
                        req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                        "POST", "/transactions/prepare", 200, 0, "", "{}", pool_ptr, logger_ptr);
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
                case TransactionStatus::NotReceiver:
                    return req->create_response(restinio::status_not_acceptable())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("sender or receiver must be whireable")
                    .done();
                    break;
                case TransactionStatus::InactiveUser:
                    return req->create_response(restinio::status_forbidden())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("user is not active")
                    .done();
                    break;
                case TransactionStatus::Cooldown:
                    return req->create_response(restinio::status_too_many_requests())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .append_header("Retry-After", std::to_string(kTransferCooldownSeconds))
                        .set_body("transfer cooldown active")
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


    void transfer(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<ws::EventBus> bus_ptr) {
        router.get()->http_post("/transactions/send", [pool_ptr, logger_ptr, bus_ptr](auto req, auto) {
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
            std::string username = auth::get_username(token, pool_ptr);
            bool sender_is_admin = auth::is_rights_by_username(username, pool_ptr);
            if (!sender_is_admin && !transactions::can_use_transactions(username, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("user is not active")
                .done();
            }

            if (new_body.HasMember("tr_id")) {
                std::string tr_id = new_body["tr_id"].GetString();
                logger_ptr->info([token, tr_id] { return fmt::format("token = {}, tr_id = {}", token, tr_id); });
                auto result = transactions::transfer_with_result(tr_id, pool_ptr);
                switch (result.status)
                {
                case TransactionStatus::Success:
                    publish_transfer_result(bus_ptr, result);
                    analytics::record_event(username, token, req->remote_endpoint().address().to_string(),
                        req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                        "POST", "/transactions/send", 200, 0, "", "{}", pool_ptr, logger_ptr);
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
                case TransactionStatus::NotReceiver:
                    return req->create_response(restinio::status_not_acceptable())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("sender or receiver must be whireable")
                    .done();
                    break;
                case TransactionStatus::InactiveUser:
                    return req->create_response(restinio::status_forbidden())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .set_body("user is not active")
                    .done();
                    break;
                case TransactionStatus::Cooldown:
                    return req->create_response(restinio::status_too_many_requests())
                        .append_header("Content-Type", "text/plain; charset=utf-8")
                        .append_header("Retry-After", std::to_string(kTransferCooldownSeconds))
                        .set_body("transfer cooldown active")
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
            if (!transactions::can_use_transactions(username, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("user is not active")
                .done();
            }
            analytics::record_event(username, token, req->remote_endpoint().address().to_string(),
                req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                "GET", "/transactions/balance", 200, 0, "", "{}", pool_ptr, logger_ptr);

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
            if (!transactions::can_use_transactions(username, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                    .append_header("Content-Type", "text/plain; charset=utf-8")
                    .set_body("user is not active")
                .done();
            }
            analytics::record_event(username, token, req->remote_endpoint().address().to_string(),
                req->header().has_field("User-Agent") ? req->header().get_field("User-Agent") : "",
                "GET", "/transactions/my", 200, 0, "", "{}", pool_ptr, logger_ptr);

            return req->create_response()
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(cp::serialize_with_shift_day(transactions::get_transactions(username, pool_ptr), pool_ptr))
                .done();
        });
    }
} // namespace transactions::server
