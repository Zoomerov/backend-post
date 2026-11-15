#include <iostream>
#include <include/bcrypt.h>
#include <curl/curl.h>
#include <jwt-cpp/jwt.h>
#include <sw/redis++/redis++.h>
#include <string>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <httplib.h>
#include <cctype>
#include <unordered_set>
#include <iterator>

class Authentication {
public:
    static std::string
    auth_login(sw::redis::Redis &redis, const std::string &username, const std::string &password,
               const std::string &secret) {
        auto hash = redis.hget("user:" + username, "password_hash");

        if (!hash || !bcrypt::validatePassword(password, *hash)) {
            throw std::runtime_error("401 Unauthorized, this login or password do not exists");
        } else {
            return create_token(username, secret);
        }
    }


    static std::string auth_registration(sw::redis::Redis &redis, const std::string &username,
                                         const std::string &password, const std::string &secret) {
        std::string hash = bcrypt::generateHash(password);

        bool created = redis.hsetnx("user:" + username, "password_hash", hash);

        if (created) {
            return create_token(username, secret);
        } else {
            throw std::runtime_error("409 Login already exists");
        }
    }


    static std::string create_token(const std::string &username, const std::string &secret) {
        if (secret.empty()) {
            throw std::runtime_error("JWT_SECRET is no set");
        }

        auto now = std::chrono::system_clock::now();

        return jwt::create()
                .set_type("JWT")
                .set_issuer("hw2")
                .set_subject(username)
                .set_issued_at(now)
                .set_expires_at(now + std::chrono::minutes{120})
                .sign(jwt::algorithm::hs256{secret});
    }


    static std::string verify_token(const std::string &token, const std::string &secret) {
        auto decoded = jwt::decode(token);
        auto verifier = jwt::verify()
                .allow_algorithm(jwt::algorithm::hs256{secret})
                .with_issuer("hw2");
        verifier.verify(decoded);

        return decoded.get_subject();
    }
};

class CoinGecko {
private:
    CURL *curl;
    curl_slist *headers = nullptr;


    picojson::value fetch_json(const std::string& url) {
        std::string body;

        const auto save_body = +[](char* data, std::size_t size,
                                  std::size_t count, void* target) -> std::size_t {
            const std::size_t bytes = size * count;
            auto& body = *static_cast<std::string*>(target);

            try {
                body.append(data, bytes);
                return bytes;
            } catch (...) {
                return 0;
            }
        };

        if (curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) != CURLE_OK ||
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, save_body) != CURLE_OK ||
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body) != CURLE_OK) {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);
            throw std::runtime_error("Failed to configure CoinGecko request");
            }

        const CURLcode result = curl_easy_perform(curl);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, stdout);

        if (result != CURLE_OK) {
            throw std::runtime_error(curl_easy_strerror(result));
        }

        picojson::value response;
        if (!picojson::parse(response, body).empty()) {
            throw std::runtime_error("Invalid CoinGecko JSON");
        }

        return response;
    }


    static std::string format_timestamp(double milliseconds) {
        const auto seconds = static_cast<std::time_t>(milliseconds / 1000);
        std::tm utc{};
        char text[32]{};

        if (gmtime_s(&utc, &seconds) != 0 ||
            std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
            throw std::runtime_error("Invalid timestamp");
            }

        return text;
    }


    static std::string uppercase(std::string text) {
        for (char& ch : text) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return text;
    }


public:
    CoinGecko(const CoinGecko &) = delete;
    CoinGecko &operator=(const CoinGecko &) = delete;


    explicit CoinGecko(const std::string &api_key) {
        const std::string key_header = "x-cg-demo-api-key: " + api_key;
        headers = curl_slist_append(nullptr, key_header.c_str());

        if (!headers) {
            curl_slist_free_all(headers);
            throw std::runtime_error("Failed to create CoinGecko request header");
        }


        curl = curl_easy_init();

        if (!curl) {
            throw std::runtime_error("Failed to initialize libcurl");
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    }


    ~CoinGecko() {
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    };



    std::string resolve_coin_id(sw::redis::Redis& redis, const std::string& symbol) {
        const auto cached = redis.hget("coingecko:symbols", symbol);
        if (cached && !cached->empty()) {
            return *cached;
        }

        if (symbol.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) {
            throw std::runtime_error("400 Invalid symbol");
        }
        const auto response = fetch_json("https://api.coingecko.com/api/v3/search?query=" + symbol);
        if (!response.is<picojson::object>() || !response.get("coins").is<picojson::array>()) {
            throw std::runtime_error("Invalid CoinGecko search response");
        }
        for (const auto& coin : response.get("coins").get<picojson::array>()) {
            if (coin.is<picojson::object>() && coin.get("symbol").is<std::string>() &&
                uppercase(coin.get("symbol").get<std::string>()) == symbol &&
                coin.get("id").is<std::string>()) {
                const auto id = coin.get("id").get<std::string>();
                redis.hset("coingecko:symbols", symbol, id);
                return id;
            }
        }
        throw std::runtime_error("404 Unknown symbol");
    }

    picojson::value get_crypto(sw::redis::Redis& redis, const std::string& username) {
        std::unordered_set<std::string> ids;
        redis.smembers("watchlist:" + username, std::inserter(ids, ids.end()));

        picojson::array cryptos;
        if (!ids.empty()) {
            std::string query;
            for (const auto& id : ids) {
                if (!query.empty()) {
                    query += ',';
                }
                query += id;
            }
            const auto response = fetch_json(
                "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=" + query);
            if (!response.is<picojson::array>()) {
                throw std::runtime_error("Invalid CoinGecko markets response");
            }
            for (const auto& coin : response.get<picojson::array>()) {
                if (!coin.is<picojson::object>() || !coin.get("symbol").is<std::string>()) {
                    throw std::runtime_error("Invalid coin data");
                }
                cryptos.emplace_back(picojson::object{
                    {"symbol", picojson::value(uppercase(coin.get("symbol").get<std::string>()))},
                    {"name", coin.get("name")},
                    {"current_price", coin.get("current_price")},
                    {"last_updated", coin.get("last_updated")}
                });
            }
        }
        return picojson::value(picojson::object{{"cryptos", picojson::value(cryptos)}});
    }

    picojson::value get_crypto() {
        const auto response = fetch_json("https://api.coingecko.com/api/v3/coins/markets"
            "?vs_currency=usd&order=market_cap_desc&per_page=250&page=1");

        if (!response.is<picojson::array>()) {
            throw std::runtime_error("Invalid CoinGecko markets response");
        }

        picojson::array cryptos;
        for (const auto& coin : response.get<picojson::array>()) {
            if (!coin.is<picojson::object>() ||
                !coin.get("symbol").is<std::string>()) {
                throw std::runtime_error("Invalid coin data");
                }

            const auto symbol = uppercase(coin.get("symbol").get<std::string>());
            cryptos.emplace_back(picojson::object{
                {"symbol", picojson::value(symbol)},
                {"name", coin.get("name")},
                {"current_price", coin.get("current_price")},
                {"last_updated", coin.get("last_updated")}
            });
        }

        return picojson::value(picojson::object{
            {"cryptos", picojson::value(cryptos)}
        });
    }


    void post_crypto(sw::redis::Redis& redis,const std::string& symbol,const std::string& username) {

        const auto id = redis.hget("coingecko:symbols", symbol);
        if (!id || id->empty()) {
            throw std::runtime_error("Symbol not found in cache");
        }

        long long added;
        try {
            added = redis.sadd("watchlist:" + username, *id);
        } catch (const sw::redis::Error& e) {
            std::cerr << "Redis error: " << e.what() << '\n';
            std::cout << "500 Internal Server Error\n";
            return;
        }

        if (added == 0) {
            std::cout << "409 Conflict: coin is already tracked\n";
            return;
        }

        std::cout << "201 Created\n";
    }


    picojson::value get_crypto_coin(sw::redis::Redis& redis, const std::string& symbol) {

        const auto id = redis.hget("coingecko:symbols", symbol);
        if (!id || id->empty()) {
            throw std::runtime_error("404, Symbol not found in cache");
        }

        const auto response = fetch_json("https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=" + *id);

        if (!response.is<picojson::array>()) {
            throw std::runtime_error("Invalid CoinGecko markets response");
        }

        const auto& coins = response.get<picojson::array>();
        if (coins.empty()) {
            throw std::runtime_error("404, Coin not found");
        }

        const auto& coin = coins.front();
        if (!coin.is<picojson::object>()) {
            throw std::runtime_error("Invalid coin data");
        }

        return picojson::value(picojson::object{
            {"symbol", picojson::value(symbol)},
            {"name", coin.get("name")},
            {"current_price", coin.get("current_price")},
            {"last_updated", coin.get("last_updated")}
        });
    }


    picojson::value put_crypto_refresh(sw::redis::Redis& redis, const std::string& symbol) {
        return picojson::value(picojson::object{
            {"crypto", get_crypto_coin(redis, symbol)}
        });
    }


    picojson::value get_crypto_history(sw::redis::Redis& redis, const std::string& symbol) {
        const auto id = redis.hget("coingecko:symbols", symbol);

        if (!id || id->empty()) {
            throw std::runtime_error("404, Symbol not found in cache");
        }

        const auto response = fetch_json( "https://api.coingecko.com/api/v3/coins/" + *id + "/market_chart?vs_currency=usd&days=7");

        if (!response.is<picojson::object>() ||
            !response.get("prices").is<picojson::array>()) {
            throw std::runtime_error("Invalid CoinGecko history response");
            }

        picojson::array history;
        for (const auto& item : response.get("prices").get<picojson::array>()) {
            if (!item.is<picojson::array>()) {
                throw std::runtime_error("Invalid price point");
            }

            const auto& point = item.get<picojson::array>();
            if (point.size() != 2 || !point[0].is<double>() || !point[1].is<double>()) {
                throw std::runtime_error("Invalid price point");
            }

            history.emplace_back(picojson::object{
                {"price", point[1]},
                {"timestamp", picojson::value(format_timestamp(point[0].get<double>()))}
            });
        }

        return picojson::value(picojson::object{
            {"symbol", picojson::value(symbol)},
            {"history", picojson::value(history)}
        });
    }


    picojson::value get_crypto_stats(sw::redis::Redis& redis,
                                 const std::string& symbol) {
        const auto response = get_crypto_history(redis, symbol);
        const auto& history = response.get("history").get<picojson::array>();

        if (history.empty()) {
            throw std::runtime_error("404, No price history");
        }

        const double first_price = history.front().get("price").get<double>();
        const double current_price = history.back().get("price").get<double>();

        double min_price = first_price;
        double max_price = first_price;
        double total = 0.0;

        for (const auto& point : history) {
            const double price = point.get("price").get<double>();
            min_price = std::min(min_price, price);
            max_price = std::max(max_price, price);
            total += price;
        }

        const double price_change = current_price - first_price;
        const auto price_change_percent = first_price == 0.0
            ? picojson::value()
            : picojson::value(100.0 * price_change / first_price);

        const picojson::object stats{
            {"min_price", picojson::value(min_price)},
            {"max_price", picojson::value(max_price)},
            {"avg_price", picojson::value(total / history.size())},
            {"price_change", picojson::value(price_change)},
            {"price_change_percent", price_change_percent},
            {"records_count", picojson::value(static_cast<double>(history.size()))}
        };

        return picojson::value(picojson::object{
            {"symbol", picojson::value(symbol)},
            {"current_price", picojson::value(current_price)},
            {"stats", picojson::value(stats)}
        });
    }


    bool delete_crypto_coin(sw::redis::Redis& redis, const std::string& symbol, const std::string& username) {

        const auto id = redis.hget("coingecko:symbols", symbol);

        if (!id || id->empty()) {
            return false;
        }

        return redis.srem("watchlist:" + username, *id) == 1;
    }
};

class Server {
private:
    sw::redis::Redis redis{"tcp://default:admin@127.0.0.1:6379"};
    const char *secret = std::getenv("JWT_SECRET");
    const char *api_key = std::getenv("COINGECKO_API_KEY");

public:

    void serve() {
        if (!secret || !*secret || !api_key || !*api_key) {
            throw std::runtime_error("JWT_SECRET and COINGECKO_API_KEY are required");
        }
        httplib::Server http;
        const auto send = [](httplib::Response& res, int status, picojson::value body) {
            res.status = status;
            res.set_content(body.serialize(), "application/json");
        };
        const auto error = [&](httplib::Response& res, int status, const std::string& message) {
            send(res, status, picojson::value(picojson::object{{"error", picojson::value(message)}}));
        };
        const auto field = [](const httplib::Request& req, const std::string& name) {
            picojson::value body;
            if (!picojson::parse(body, req.body).empty() || !body.is<picojson::object>() ||
                !body.get(name).is<std::string>() || body.get(name).get<std::string>().empty()) {
                throw std::runtime_error("400 Missing or invalid " + name);
            }
            return body.get(name).get<std::string>();
        };
        const auto handle = [&](auto action, bool authenticate = true) {
            return [&, action, authenticate](const httplib::Request& req, httplib::Response& res) {
                try {
                    std::string username;
                    if (authenticate) {
                        const auto header = req.get_header_value("Authorization");
                        if (header.starts_with("Bearer ")) {
                            try {
                                username = Authentication::verify_token(header.substr(7), secret);
                            } catch (const std::exception&) {
                                error(res, 401, "Invalid token");
                                return;
                            }
                        }
                        if (username.empty() || !redis.exists("user:" + username)) {
                            error(res, 401, "Unauthorized");
                            return;
                        }
                    }
                    action(req, res, username);
                } catch (const std::exception& e) {
                    const std::string message = e.what();
                    for (const int status : {400, 401, 404, 409}) {
                        if (message.starts_with(std::to_string(status))) {
                            error(res, status, message);
                            return;
                        }
                    }
                    error(res, 500, "Internal server error");
                }
            };
        };
        http.Post("/auth/register", handle([&](const auto& req, auto& res, const auto&) {
            const auto token = Authentication::auth_registration(
                redis, field(req, "username"), field(req, "password"), secret);
            send(res, 201, picojson::value(picojson::object{{"token", picojson::value(token)}}));
        }, false));
        http.Post("/auth/login", handle([&](const auto& req, auto& res, const auto&) {
            const auto token = Authentication::auth_login(
                redis, field(req, "username"), field(req, "password"), secret);
            send(res, 200, picojson::value(picojson::object{{"token", picojson::value(token)}}));
        }, false));
        http.Post("/crypto", handle([&](const auto& req, auto& res, const auto& username) {
            CoinGecko gecko(api_key);
            auto symbol = field(req, "symbol");
            for (char& ch : symbol) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            const auto id = gecko.resolve_coin_id(redis, symbol);
            if (redis.sismember("watchlist:" + username, id)) {
                throw std::runtime_error("409 Coin already tracked");
            }
            auto response = gecko.put_crypto_refresh(redis, symbol);
            if (!redis.sadd("watchlist:" + username, id)) {
                throw std::runtime_error("409 Coin already tracked");
            }
            send(res, 201, response);
        }));
        const auto tracked = [&](const std::string& symbol, const std::string& username) {
            const auto id = redis.hget("coingecko:symbols", symbol);
            if (!id || !redis.sismember("watchlist:" + username, *id)) {
                throw std::runtime_error("404 Coin not found in watchlist");
            }
        };
        http.Get("/crypto", handle([&](const auto&, auto& res, const auto& username) {
            CoinGecko gecko(api_key);
            send(res, 200, gecko.get_crypto(redis, username));
        }));
        http.Get(R"(/crypto/([^/]+))", handle([&](const auto& req, auto& res, const auto& username) {
            const auto symbol = req.matches[1].str();
            tracked(symbol, username);
            CoinGecko gecko(api_key);
            send(res, 200, gecko.get_crypto_coin(redis, symbol));
        }));
        http.Put(R"(/crypto/([^/]+)/refresh)", handle([&](const auto& req, auto& res, const auto& username) {
            const auto symbol = req.matches[1].str();
            tracked(symbol, username);
            CoinGecko gecko(api_key);
            send(res, 200, gecko.put_crypto_refresh(redis, symbol));
        }));
        http.Get(R"(/crypto/([^/]+)/history)", handle([&](const auto& req, auto& res, const auto& username) {
            const auto symbol = req.matches[1].str();
            tracked(symbol, username);
            CoinGecko gecko(api_key);
            send(res, 200, gecko.get_crypto_history(redis, symbol));
        }));
        http.Get(R"(/crypto/([^/]+)/stats)", handle([&](const auto& req, auto& res, const auto& username) {
            const auto symbol = req.matches[1].str();
            tracked(symbol, username);
            CoinGecko gecko(api_key);
            send(res, 200, gecko.get_crypto_stats(redis, symbol));
        }));
        http.Delete(R"(/crypto/([^/]+))", handle([&](const auto& req, auto& res, const auto& username) {
            CoinGecko gecko(api_key);
            if (!gecko.delete_crypto_coin(redis, req.matches[1].str(), username)) {
                throw std::runtime_error("404 Coin not found in watchlist");
            }
            send(res, 200, picojson::value(picojson::object{}));
        }));
        if (!http.listen("127.0.0.1", 8080)) {
            throw std::runtime_error("Cannot listen on port 8080");
        }
    }

    void parsing(int argc, char *argv[]) {
        if (argc >= 3) {
            std::string method = argv[1];
            std::string path = argv[2];



            // POST /auth/login
            if (method == "POST" && path == "/auth/login") {
                std::string username, password;

                std::cout << "Enter your username: ";
                std::cin >> username;

                std::cout << "Enter your password: ";
                std::cin >> password;

                if (username.empty() || password.empty()) {
                    throw std::runtime_error("400 Bad request, empty login or password");
                }
                std::cout << Authentication::auth_login(redis, username, password, secret) << '\n';



                // POST /auth/register
            } else if (method == "POST" && path == "/auth/register") {
                std::string username, password;

                std::cout << "Enter your username: ";
                std::cin >> username;

                std::cout << "Enter your password: ";
                std::cin >> password;

                if (username.empty() || password.empty()) {
                    throw std::runtime_error("400 Bad request, empty login or password");
                }
                Authentication::auth_registration(redis, username, password, secret);



                // GET /crypto <token>
            } else if (method == "GET" && path == "/crypto") {
                if (argc > 4) {
                    throw std::runtime_error("More than needed args for get /crypto request");
                }

                if (argv[3] == nullptr) {
                    throw std::runtime_error("need token as an argument");
                }

                if (checking_validation_token(argv[3]) == true) {
                    CoinGecko gecko(api_key);
                    std::cout << gecko.get_crypto().serialize() << '\n';
                }else {
                    throw std::runtime_error("401, Unauthorized user token");
                }



                // POST /crypto <token>
            } else if (method == "POST" && path == "/crypto") {
                if (argc > 5) {
                    throw std::runtime_error("More than needed args for get /crypto request");
                }

                if (argv[3] == nullptr) {
                    throw std::runtime_error("need token as an argument");
                }


                if (checking_validation_token(argv[3]) == true) {
                    CoinGecko gecko(api_key);
                    std::cout << "Enter coin_name: ";
                    std::string symbol; std::cin >> symbol;
                    gecko.post_crypto(redis, symbol, Authentication::verify_token(argv[3], secret));
                }else {
                    throw std::runtime_error("401, Unauthorized user token");
                }



                //GET /crypto/symbol/history
            }else if (method == "GET" && path.starts_with("/crypto/") && path.ends_with("/history") && path.size() > 16) {
                const auto symbol = path.substr(8,path.size()-16);

                if (symbol.empty() || symbol.find('/') != std::string::npos) {
                    throw std::runtime_error("Expected /crypto/SYMBOL 2");
                }

                if (argv[3] == nullptr) {
                    throw std::runtime_error("need token as an argument");
                }

                if (checking_validation_token(argv[3]) == true) {
                    CoinGecko gecko(api_key);
                    std::cout << gecko.get_crypto_history(redis, symbol).serialize() << '\n';
                }else {
                    throw std::runtime_error("401, Unauthorized user token");
                }



                //GET /crypto/symbol/stats <token>
            }else if (method == "GET" && path.starts_with("/crypto/") && path.ends_with("/stats") && path.size() > 14) {
                const auto symbol = path.substr(8,path.size()-14);

                if (symbol.find('/') != std::string::npos) {
                    throw std::runtime_error("Expected /crypto/SYMBOL/stats");
                }

                if (argc != 4) {
                    throw std::runtime_error("Usage: hw2.exe GET /crypto/SYMBOL/stats JWT");
                }

                if (!checking_validation_token(argv[3])) {
                    throw std::runtime_error("401, Unauthorized user token");
                }

                CoinGecko gecko(api_key);
                std::cout << gecko.get_crypto_stats(redis, symbol).serialize() << '\n';




                // GET /crypto/symbol/ <token>
            } else if (method == "GET" && path.starts_with("/crypto/")) {
                const auto symbol = path.substr(8);
                if (symbol.empty() || symbol.find('/') != std::string::npos) {
                    throw std::runtime_error("Expected /crypto/SYMBOL");
                }

                if (argv[3] == nullptr) {
                    throw std::runtime_error("need token as an argument");
                }

                if (checking_validation_token(argv[3]) == true) {
                    CoinGecko gecko(api_key);
                    std::cout << gecko.get_crypto_coin(redis, symbol).serialize() << '\n';
                }else {
                    throw std::runtime_error("401, Unauthorized user token");
                }



                // PUT /crypto/symbol/refresh <token>
            } else if (method == "PUT" && path.starts_with("/crypto/") && path.ends_with("/refresh") && path.size() > 16) {
                const auto symbol = path.substr(8,path.size()-16);

                if (symbol.empty() || symbol.find('/') != std::string::npos) {
                    throw std::runtime_error("Expected /crypto/SYMBOL 1");
                }

                if (argv[3] == nullptr) {
                    throw std::runtime_error("need token as an argument");
                }

                if (checking_validation_token(argv[3]) == true) {
                    CoinGecko gecko(api_key);
                    const auto response = gecko.put_crypto_refresh(redis, symbol);
                    std::cout << response.serialize() << '\n';
                }else {
                    throw std::runtime_error("401, Unauthorized user token");
                }



                // DELETE /crypto/symbol <token>
            } else if (method == "DELETE" && path.starts_with("/crypto/")) {
                const auto symbol = path.substr(8);
                if (symbol.empty() || symbol.find('/') != std::string::npos) {
                    throw std::runtime_error("Expected /crypto/SYMBOL");
                }

                if (argc != 4) {
                    throw std::runtime_error("Usage: hw2.exe DELETE /crypto/SYMBOL JWT");
                }

                const auto username = Authentication::verify_token(argv[3], secret);
                if (username.empty() || redis.exists("user:" + username) == 0) {
                    throw std::runtime_error("401, Unauthorized user token");
                }

                CoinGecko gecko(api_key);

                if (gecko.delete_crypto_coin(redis, symbol, username)) {
                    std::cout << "200. OK\n";
                } else {
                    std::cout << "404. Coin not found in watchlist" << '\n';
                }

            }
            else {
                throw std::runtime_error("400 Bad request, Unknown method or path");
            }
        } else {
            throw std::runtime_error("Less then needed args");
        }
    }

    bool checking_validation_token(const std::string &jwt_token) {
        const auto username = Authentication::verify_token(jwt_token, secret);
        if (username.empty() || redis.exists("user:" + username) == 0) {
            return false;
        }
        return true;
    }
};


int main(int argc, char *argv[]) {
    try {
        Server s;
        if (argc == 1) {
            s.serve();
        } else {
            s.parsing(argc, argv);
        }
    } catch (const std::exception &e) {
        std::cout << "CRASH! " << e.what();
        return 1;
    }
    return 0;
}
