#include "auth.h"

#include <iostream>

bool is_authed(std::string token, std::shared_ptr<cp::connection_pool> pool_ptr) { 
    auto decoded = jwt::decode(token);
    std::string user;
    if (decoded.has_id())
        user = decoded.get_id();
    else return false;

    // if user in db true, else false 
    cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1);");

    auto tx = cp::tx(*pool_ptr, get_user);
    
    pqxx::result result = get_user(user);

    if(result.empty()) {
        return false;
    }
    return true;
}


std::unique_ptr<restinio::router::express_router_t<>> enable_auth_reg(std::unique_ptr<restinio::router::express_router_t<>> svr_ptr, std::shared_ptr<cp::connection_pool> pool_ptr) {
    svr_ptr->http_post("/auth", [pool_ptr](auto req, auto) {
        std::string endpoint = req->remote_endpoint().address().to_string();
        // spdlog::info("auth request from " + endpoint);

        rapidjson::Document new_body;
        new_body.Parse(req->body().c_str());

        if (new_body.HasMember("login") && new_body.HasMember("password")) {
            std::string loginHeader = new_body["login"].GetString();
            std::string passwordHeader = new_body["password"].GetString();

            // get password from db, compare hash
            std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

            cp::query get_user("SELECT * FROM \"user\" WHERE \"username\"=($1) AND \"passwdhash\"=($2);");
            auto tx = cp::tx(*pool_ptr, get_user);
            pqxx::result result = get_user(loginHeader, passwordHash);
            
            if(result.empty()) {
                // spdlog::info("empty headers from " + endpoint);
                // TODO: wrong status, change
                return req->create_response(restinio::status_non_authoritative_information()) .append_header_date_field().connection_close().done();
                
            }
            else {
                auto token = jwt::create()
                    .set_type("JWS")
                    .set_issuer("auth0")
                    .set_id(loginHeader)
                    .sign(jwt::algorithm::hs256{"secret"});
                return req->create_response().set_body("{\"token\": \"" + token + "\"}").done();

        //         res.set_header("token", token);
        //         res.set_content(token, "text/plain");
                
            }
        }
        
        else {
            return req->create_response(restinio::status_non_authoritative_information()) .append_header_date_field().connection_close().done();
        }
        // spdlog::info(endpoint + " is authed");
        // res.status = status;
    }); 


    // svr_ptr->Post("/reg", [pool_ptr](const httplib::Request& req, httplib::Response& res){
    //     // spdlog::info("reg request from " + req.remote_addr);

    //     int status = 200;

    //     rapidjson::Document new_body;
    //     new_body.Parse(req.body.c_str());

    //     if (new_body.HasMember("login") && new_body.HasMember("password"))
    //     {
    //         std::string loginHeader = new_body["login"].GetString();
    //         std::string passwordHeader = new_body["password"].GetString();

    //         try {
    //             // create password hash, write info to db
    //             std::string passwordHash = std::to_string(std::hash<std::string>{}(passwordHeader));

    //             cp::query add_user("INSERT INTO \"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());");

    //             auto tx = cp::tx(*pool_ptr, add_user);

    //             pqxx::result result = add_user(std::stoul(passwordHash) % 10000, loginHeader, passwordHash);      

    //             tx.commit();

    //             auto token = jwt::create()
    //                 .set_type("JWS")
    //                 .set_issuer("auth0")
    //                 .set_id(loginHeader)
    //                 .sign(jwt::algorithm::hs256{"secret"});  
    //         }
    //         catch(const char* error_message) {std::cout<<error_message<<std::endl;} 
    //         // spdlog::info("new user " + loginHeader);
    //         res.set_content("ok", "text/plain");
    //     }
    //     else
    //     {
    //         res.set_content("probably wrong body names", "text/plain");
    //         status = 403;
    //     }
    //     res.status = status;
    // });

    // svr_ptr -> Get("/amiauthed", [&](const httplib::Request& req, httplib::Response& res) {
    //     if(is_authed(req, pool_ptr)) {
    //         res.status = 200;
    //     } else {
    //         res.status = 403;
    //     }
    // });
    return svr_ptr;
}

// DELETE ME
void test_http(cp::connection_pool& pool_ptr) {
    cp::query add_user("INSERT INTO public.\"user\" (userid, username, passwdhash, userrights, jointime) VALUES($1, $2, $3, '', now());");

    auto tx = cp::tx(pool_ptr, add_user);

    pqxx::result result = add_user(123, "scv2", "password2");

    tx.commit();
}