#include "DOM.h"

namespace actives::deals {
    DOM::DOM(std::shared_ptr<cp::ConnectionsManager> pool_ptr) : pool_ptr(pool_ptr) {};

    bool DOM::add_bid(std::shared_ptr<cp::ConnectionsManager> pool_ptr, int bid_id, std::string user_name, int price, int amount, actives::active_obj act) {
        int total = price * amount;
        // TODO reciever username
        if(transactions::transfer(
            transactions::prepare_transaction(
                user_name, 
                Config::giveMe().market_config.junk_user,
                total,
                pool_ptr
            ).second,
            pool_ptr
        ) == transactions::TransactionStatus::Success) {
            bids[price].push(bid(price, bid_id, user_name, act, amount));
            return true;
        }
        return false;
    }

    std::vector<bid> DOM::check_bids() {
        auto start = bids.begin();
        auto end = bids.end();
        std::vector<bid> res;

        while (start != end && start -> first < 0 && end -> first > 0) {
            if (start -> first * (-1) < end -> first) {
                ++start;
            } else if (start -> first * (-1) > end -> first) {
                --end;
            } else if (start -> first * (-1) == end -> first) {
                while(!start -> second.empty() && !end -> second.empty()) {
                    auto b1 = start -> second.top();
                    auto b2 = end -> second.top();
                    if (b1->bidVal.price == b2->bidVal.price) {
                        int ammount = std::min(b1->bidVal.amount, b2->bidVal.amount);
                        res.push_back(bid(
                            b1->bidVal.price, 
                            b1->bidVal.bid_id, 
                            b1->bidVal.owner, 
                            b1->bidVal.active, 
                            ammount, 
                            true
                        ));
                        b1->bidVal.amount -= ammount;
                        res.push_back(bid(
                            b2->bidVal.price, 
                            b2->bidVal.bid_id, 
                            b2->bidVal.owner, 
                            b2->bidVal.active, 
                            ammount, 
                            true
                        ));
                        b2->bidVal.amount -= ammount;
                        if (b1->bidVal.amount == 0) {
                            start -> second.pop();
                        }
                        if (b2->bidVal.amount == 0) {
                            end -> second.pop();
                        }
                    }
                }
            }
            
        }
        return res;
    }
    
    void DOM::remove_bid(std::shared_ptr<cp::ConnectionsManager> pool_ptr, int price, int bid_id) {
        if (bids.find(price) == bids.end()) {
            return;
        }
        bid bidToDelete = bids[price].delBid(bid_id);
        transactions::transfer(
            transactions::prepare_transaction(
                Config::giveMe().market_config.junk_user, 
                bidToDelete.owner, 
                bidToDelete.amount * bidToDelete.price, 
                pool_ptr
            ).second,
            pool_ptr
        );
    }

    void DOM::remove_bid(std::shared_ptr<cp::ConnectionsManager> pool_ptr, bid b) {
        remove_bid(pool_ptr, b.price, b.bid_id);
    }

    void DOM::resolve_bids(std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::vector<bid> res = check_bids();
        for (auto b : res) {
            actives::add_active(pool_ptr, b.owner, b.active);
            DOM::remove_bid(pool_ptr, b);
        }
    }

    void DOM::start_resolver() {
        std::thread t([this] {
            while (true) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(
                        Config::giveMe().market_config.bid_resolver_sleep_time
                    )
                );
                resolve_bids(pool_ptr);
            }
        });
        t.detach();
    }


    std::vector<bid> DOM::users_bids(std::string user_name) {
        std::vector<bid> res;
        for (auto& [price, bids] : bids) {
            for (auto& b : bids.byUser(user_name)) {
                res.push_back(b);
            }
        }
        return res;
    }
}

namespace actives::deals::server {
    void add_bid(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<actives::deals::DOM> dom_ptr) {
        router.get()->http_post("/deals/add_bid", [pool_ptr, logger_ptr, dom_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /deals/add_bid";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().data());
            if (!new_body.HasMember("price") || !new_body.HasMember("amount") || !new_body.HasMember("active")) {
                return req->create_response(restinio::status_bad_request())
                    .done();
            }
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string user_name = auth::get_username(token, pool_ptr);
            int price = new_body["price"].GetInt();
            int amount = new_body["amount"].GetInt();
            auto now = std::chrono::system_clock::now();
            auto now_time_t = std::chrono::system_clock::to_time_t(now);
            int bid_id = std::stoi(hashing::hash_from_string(user_name + std::to_string(now_time_t)).substr(0, 10));
            actives::active_obj act;
            if (new_body["active"].IsInt()) {
                act.activeId = new_body["active"].GetInt();
            }
            if (new_body["active"].IsString()) {
                act.activeTicker = new_body["active"].GetString();
            }
            if (dom_ptr->add_bid(pool_ptr, bid_id, user_name, price, amount, act)) {
                return req->create_response()
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("ok")
                .done();
            } else {
                return req->create_response()
                .append_header("Content-Type", "text/plain; charset=utf-8")
                .set_body("no money")
                .done();
            }
        });
    }
    void remove_bid(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<actives::deals::DOM> dom_ptr) {
        router.get()->http_delete("/deals/remove_bid", [pool_ptr, logger_ptr, dom_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /deals/remove_bid";});
            rapidjson::Document new_body;
            new_body.Parse(req->body().data());
            if (!new_body.HasMember("price") || !new_body.HasMember("bid_id")) {
                return req->create_response(restinio::status_bad_request())
                .done();
            }
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string user_name = auth::get_username(token, pool_ptr);
            int price = new_body["price"].GetInt();
            int bid_id = new_body["bid_id"].GetInt();
            dom_ptr->remove_bid(pool_ptr, price, bid_id);
            return req->create_response()
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body("ok")
                .done();
        });
    }

    void users_bids(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr, std::shared_ptr<actives::deals::DOM> dom_ptr) {
        router.get()->http_get("/deals/users_bids", [pool_ptr, logger_ptr, dom_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /deals/users_bids";});
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (token.empty()) {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }
            std::string user_name = auth::get_username(token, pool_ptr);
            if(user_name == "") {
                return req->create_response(restinio::status_unauthorized())
                .done();
            }
            return req->create_response()
                .append_header("Content-Type", "application/json; charset=utf-8")
                .set_body(actives::deals::serialaze(dom_ptr->users_bids(user_name)))
                .done();
        });
    }

    void enable_bids(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        std::shared_ptr<DOM> dom_ptr = std::make_shared<DOM>(pool_ptr);

        add_bid(router, pool_ptr, logger_ptr, dom_ptr);

        remove_bid(router, pool_ptr, logger_ptr, dom_ptr);

        dom_ptr -> start_resolver();
    }
}