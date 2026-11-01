#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <curl/curl.h>
#include <chrono>


class Hedgedcurl {
private:
    int argc;
    char **argv;

public:
    ~Hedgedcurl() = default;

    Hedgedcurl(int argc_, char *argv_[]) {
        argc = argc_;
        argv = argv_;
    }

    Hedgedcurl(const Hedgedcurl &) = delete;

    Hedgedcurl &operator=(const Hedgedcurl &) = delete;

    bool isHttpURL(const std::string &arg) const {
        CURLU *url = curl_url();
        if (url == nullptr) {
            return false;
        }

        CURLUcode result = curl_url_set(url, CURLUPART_URL, arg.c_str(), 0);

        bool valid = false;
        char *scheme = nullptr;

        if (result == CURLUE_OK && curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK) {
            std::string protocol(scheme);
            valid = (protocol == "http" || protocol == "https");
        }

        curl_free(scheme);
        curl_url_cleanup(url);

        return valid;
    }

    static std::size_t saveData(const char *data, std::size_t size, std::size_t count, void *userdata) {
        auto *buffer = static_cast<std::string *>(userdata);
        std::size_t bytes = size * count;

        buffer->append(data, bytes);
        return bytes;
    }

    int sendRequests(const std::vector<std::string> &urls, long seconds) const {
        struct Request {
            CURL *handle = nullptr;
            std::string headers;
            std::string body;
        };

        std::vector<Request> requests(urls.size());

        CURLM *multi = curl_multi_init();

        if (!multi) {
            throw std::runtime_error("Cannot create multi handle\n");
        }

        for (std::size_t i = 0; i < urls.size(); ++i) {
            auto &request = requests[i];
            request.handle = curl_easy_init();

            if (!request.handle) {
                continue;
            }

            curl_easy_setopt(request.handle, CURLOPT_URL, urls[i].c_str());
            curl_easy_setopt(request.handle, CURLOPT_TIMEOUT, seconds);

            curl_easy_setopt(request.handle, CURLOPT_WRITEFUNCTION, saveData);
            curl_easy_setopt(request.handle, CURLOPT_WRITEDATA, &request.body);

            curl_easy_setopt(request.handle, CURLOPT_HEADERFUNCTION, saveData);
            curl_easy_setopt(request.handle, CURLOPT_HEADERDATA, &request.headers);

            curl_multi_add_handle(multi, request.handle);
        }

        CURL *winner = nullptr;
        int running = 0;
        bool hadTimeout = false;

        while (true) {
            CURLMcode result = curl_multi_perform(multi, &running);
            if (result != CURLM_OK) {
                break;
            }

            int remaining = 0;
            CURLMsg *message;

            while ((message = curl_multi_info_read(multi, &remaining))) {
                if (message->msg == CURLMSG_DONE && message->data.result == CURLE_OK) {
                    winner = message->easy_handle;
                    break;
                }

                if (message->data.result == CURLE_OPERATION_TIMEDOUT) {
                    hadTimeout = true;
                }
            }

            if (winner || running == 0) {
                break;
            }

            result = curl_multi_poll(multi, nullptr, 0, 1000, nullptr);
            if (result != CURLM_OK) {
                break;
            }
        }

        for (auto &request: requests) {
            if (request.handle == winner) {
                std::cout << request.headers << request.body;
            }
        }

        for (auto &request: requests) {
            if (request.handle) {
                curl_multi_remove_handle(multi, request.handle);
                curl_easy_cleanup(request.handle);
            }
        }

        curl_multi_cleanup(multi);

        if (!winner) {
            std::cerr << "No successful responce\n";
            return hadTimeout ? 228 : 1;
        }

        return 0;
    }


    int Start() const {
        //пустой запрос
        if (argc <= 1) {
            // никогда не сработает?
            throw std::runtime_error("No command after hedgedcurl\n");
        }

        //проверка на --help
        if (argc >= 2 && argv[1] != nullptr) {
            if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
                //проверка, что у нас после --help нет аргументов
                if (argv[2] != nullptr) {
                    // после help есть аргументы, ошибка
                    throw std::runtime_error(std::string("too many arguments for --help\n"));
                }

                std::ifstream file("HelpHedgedcurl.txt");
                if (!file.is_open()) {
                    throw std::runtime_error("Opening error -> HelpHedgedcurl.txt\n");
                }

                std::cout << file.rdbuf();
                file.close();
                return 0;
            }
        }
        int seconds = 15;
        int firstUrl = 1;
        // get запрос с timeout
        if ((std::string(argv[1]) == "-t" || std::string(argv[1]) == "--timeout")) {
            if (argc < 4) {
                throw std::runtime_error("Expected timeout and URL");
            }

            std::string value(argv[2]);
            std::size_t parsed = 0;
            seconds = std::stoi(value, &parsed);

            if (parsed != value.size() || seconds <= 0) {
                throw std::runtime_error("Invalid timeout");
            }

            firstUrl = 3;
        }

        std::vector<std::string> urls;

        for (int i = firstUrl; i < argc; ++i) {
            std::string url(argv[i]);
            if (isHttpURL(url)) {
                urls.push_back(url);
            } else {
                std::cerr << "Unavalable parametr for urls\n" << url << '\n';
            }
        }

        if (urls.empty()) {
            throw std::runtime_error("No valid URLs");
        }

        return sendRequests(urls, seconds);
    }
};

int main(int argc, char *argv[]) {
    try {
        Hedgedcurl hedgedcurl(argc, argv);
        return hedgedcurl.Start();
    } catch (const std::runtime_error &e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}
