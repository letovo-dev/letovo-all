#include <iostream>
#include <restinio/all.hpp>
#include <restinio/tls.hpp>
#include "rapidjson/document.h"

// do i need this?
#include <fmt/format.h>
#include <fmt/ostream.h>

#include "./basic/checks.h"

// server functions
#include "./letovo-soc-net/achivements.h"
#include "./basic/auth.h"
#include "./basic/config.h"
#include "./basic/media.h"
#include "./letovo-soc-net/page_server.h"
#include "./letovo-soc-net/authors.h"
#include "./basic/user_data.h"
#include "./market/transactions.h"
#include "./letovo-soc-net/social.h"

#include <filesystem>
namespace fs = std::filesystem;

using namespace restinio;

namespace rr = restinio::router;
using router_t = rr::express_router_t<>;

void hi(
    std::unique_ptr<restinio::router::express_router_t<>>& router,
    std::shared_ptr<cp::ConnectionsManager>& pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr
) {
    router.get()->http_get("/hi", [logger_ptr](auto req, auto) {
        asio_ns::ip::tcp::endpoint endpoint = req->remote_endpoint();

        logger_ptr->info([] { return fmt::format("hi recieved"); });

        std::cout << "endpoint: " << endpoint.address().to_string() << std::endl;
        return req->create_response().set_body(endpoint.address().to_string()).done();
    });
}

std::unique_ptr<restinio::router::express_router_t<>> create(
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr
) {
    auto router = std::make_unique<router::express_router_t<>>();

    hi(router, pool_ptr, logger_ptr);

    page::server::get_page_content(router, pool_ptr, logger_ptr);
    page::server::get_page_author(router, pool_ptr, logger_ptr);
    page::server::add_page(router, pool_ptr, logger_ptr);
    page::server::update_likes(router, pool_ptr, logger_ptr);
    page::server::delete_post(router, pool_ptr, logger_ptr);

    page::server::get_favourite_posts(router, pool_ptr, logger_ptr);
    page::server::post_add_favourite_post(router, pool_ptr, logger_ptr);
    page::server::delete_favourite_post(router, pool_ptr, logger_ptr);
    page::server::update_post(router, pool_ptr, logger_ptr);
    page::server::rename_category(router, pool_ptr, logger_ptr);

    auth::server::enable_reg(router, pool_ptr, logger_ptr);
    auth::server::enable_auth(router, pool_ptr, logger_ptr);
    auth::server::add_userrights(router, pool_ptr, logger_ptr);
    auth::server::am_i_authed(router, pool_ptr, logger_ptr);
    auth::server::am_i_admin(router, pool_ptr, logger_ptr);
    auth::server::enable_delete(router, pool_ptr, logger_ptr);
    auth::server::is_user_active(router, pool_ptr, logger_ptr);
    auth::server::is_user(router, pool_ptr, logger_ptr);
    auth::server::change_password(router, pool_ptr, logger_ptr);
    auth::server::change_username(router, pool_ptr, logger_ptr);
    auth::server::register_true(router, pool_ptr, logger_ptr);
    auth::server::is_admin(router, pool_ptr, logger_ptr);

    user::server::user_info(router, pool_ptr, logger_ptr);
    user::server::full_user_info(router, pool_ptr, logger_ptr);
    user::server::user_roles(router, pool_ptr, logger_ptr);
    user::server::user_unactive_roles(router, pool_ptr, logger_ptr);
    user::server::add_user_role(router, pool_ptr, logger_ptr);
    user::server::delete_user_role(router, pool_ptr, logger_ptr);
    user::server::create_role(router, pool_ptr, logger_ptr);
    user::server::department_roles(router, pool_ptr, logger_ptr);
    user::server::department_name(router, pool_ptr, logger_ptr);
    user::server::set_users_department(router, pool_ptr, logger_ptr);
    user::server::all_departments(router, pool_ptr, logger_ptr);
    user::server::starter_role(router, pool_ptr, logger_ptr);
    user::server::all_avatars(router, logger_ptr);
    user::server::set_avatar(router, pool_ptr, logger_ptr);

    transactions::server::prepare_transaction(router, pool_ptr, logger_ptr);
    transactions::server::transfer(router, pool_ptr, logger_ptr);
    transactions::server::get_balance(router, pool_ptr, logger_ptr);
    transactions::server::get_transactions(router, pool_ptr, logger_ptr);

    achivements::server::user_achivemets(router, pool_ptr, logger_ptr);
    achivements::server::add_achivement(router, pool_ptr, logger_ptr);
    achivements::server::delete_achivement(router, pool_ptr, logger_ptr);
    achivements::server::achivements_tree(router, pool_ptr, logger_ptr);
    achivements::server::achivement_info(router, pool_ptr, logger_ptr);
    achivements::server::create_achivement(router, pool_ptr, logger_ptr);
    achivements::server::full_user_achivemets(router, pool_ptr, logger_ptr);
    achivements::server::achivement_pictures(router, logger_ptr);
    achivements::server::no_department_achivements(router, pool_ptr, logger_ptr);
    achivements::server::department_achivements_by_user(router, pool_ptr, logger_ptr);
    achivements::server::qr_code_by_achivement(router, pool_ptr, logger_ptr);

    social::server::get_authors_list(router, pool_ptr, logger_ptr);
    social::server::get_news(router, pool_ptr, logger_ptr);
    social::server::get_all_posts(router, pool_ptr, logger_ptr);
    social::server::get_comments(router, pool_ptr, logger_ptr);
    social::server::get_post_media(router, pool_ptr, logger_ptr);
    social::server::add_like(router, pool_ptr, logger_ptr);
    social::server::add_dislike(router, pool_ptr, logger_ptr);
    social::server::add_comment(router, pool_ptr, logger_ptr);
    social::server::get_post(router, pool_ptr, logger_ptr);
    social::server::get_all_titles(router, pool_ptr, logger_ptr);
    social::server::search_by_title(router, pool_ptr, logger_ptr);
    social::server::delete_dislike(router, pool_ptr, logger_ptr);
    social::server::delete_like(router, pool_ptr, logger_ptr);
    social::server::get_saved_posts(router, pool_ptr, logger_ptr);
    social::server::save_post(router, pool_ptr, logger_ptr);
    social::server::delete_saved_post(router, pool_ptr, logger_ptr);
    social::server::get_post_categories(router, pool_ptr, logger_ptr);
    social::server::get_post_by_category(router, pool_ptr, logger_ptr);

    authors::server::get_avaluable_authors(router, pool_ptr, logger_ptr);

    media::server::get_file(router, pool_ptr, logger_ptr);

    return router;
}

void prepare_paths() {
    for (auto path : Config::giveMe().required_paths) {
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
    }
}

int main() {
    using namespace std::chrono;

    auto logger_ptr = std::make_shared<restinio::shared_ostream_logger_t>();

    std::shared_ptr<cp::ConnectionsManager> pool_ptr =
        std::make_shared<cp::ConnectionsManager>(logger_ptr, Config::giveMe().sql_config);

    prepare_paths();

    pool_ptr->connect();

    pre_run_checks::do_checks(pool_ptr);

    auto router = create(pool_ptr, logger_ptr);

    using traits_t = restinio::single_thread_tls_traits_t<
        restinio::asio_timer_manager_t,
        restinio::single_threaded_ostream_logger_t,
        restinio::router::express_router_t<>>;

    struct traits : public default_traits_t {
        using request_handler_t = restinio::router::express_router_t<>;
    };

    logger_ptr->info([] {
        return fmt::format("Server is starting at {}:{}", Config::giveMe().server_config.adress, Config::giveMe().server_config.port);
    });

    restinio::run(restinio::on_thread_pool<traits>(Config::giveMe().server_config.thread_pool_size)
                      .address(Config::giveMe().server_config.adress)
                      .port(Config::giveMe().server_config.port)
                      .request_handler(move(router))
                  // .tls_context( std::move(tls_context) )
    );
    return 0;
}
