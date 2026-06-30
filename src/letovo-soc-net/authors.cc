#include "authors.h"

namespace authors {

    pqxx::result get_avaluable_authors(std::string username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username};
        pqxx::result result = con->execute_params("SELECT username, avatar_pic, display_name FROM get_users_by_role($1);", params);
        pool_ptr->returnConnection(std::move(con));
        return result;
    }

    bool check_if_avaluable_author(std::string username, std::string author, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        if (username == author) {
            return true;
        }
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {username, author};
        pqxx::result result = con->execute_params("SELECT can_publish_as($1, $2);", params);
        pool_ptr->returnConnection(std::move(con));
        return result[0][0].as<bool>();
    }
} // namespace authors

namespace authors::server {

    void get_avaluable_authors(std::unique_ptr<restinio::router::express_router_t<>>& router, std::shared_ptr<cp::ConnectionsManager> pool_ptr, std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get("/authors_list", [pool_ptr, logger_ptr](auto req, auto) {
            std::string token = security::bearer_or_cookie_token(req->header());
            if(token.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            if (token.empty()) {
                logger_ptr->info([]{return "token is empty";});
                return req->create_response(restinio::status_unauthorized()).done();
            }

            pqxx::result result = authors::get_avaluable_authors(auth::get_username(token, pool_ptr), pool_ptr);
            
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }

} // namespace authors::server