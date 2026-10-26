#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <functional>
#include <string>
#include <stdexcept>

using namespace std;

class Server {
private:
    WSADATA wsaData;
    ADDRINFO hints;
    ADDRINFO *addrResult = nullptr;
    SOCKET ClientSocket = INVALID_SOCKET;
    SOCKET ListenSocket = INVALID_SOCKET;
    static constexpr int delayForAnswer = 1000;

    const char *sendBuffer = "OK\n";

    char recvBuffer[512];

public:
    Server() {
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET; //  ipv4
        hints.ai_socktype = SOCK_STREAM; // сокет
        hints.ai_protocol = IPPROTO_TCP; // протокол tcp

        //создание сокета
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            cerr << "WSAStartup failed, result = " << result << endl;
            throw std::runtime_error(std::string("WSAStartup failed with error: ") + std::to_string(result));
        }
    }

    Server(const Server &) = delete;

    Server &operator=(const Server &) = delete;

    ~Server() {
        freeaddrinfo(addrResult);

        if (ClientSocket != INVALID_SOCKET) {
            closesocket(ClientSocket);
            ClientSocket = INVALID_SOCKET;
        }

        if (ListenSocket != INVALID_SOCKET) {
            closesocket(ListenSocket);
            ListenSocket = INVALID_SOCKET;
        }

        WSACleanup();
        addrResult = nullptr;
    }


    void CheckAddres() {
        int result = getaddrinfo("localhost", "8080", &hints, &addrResult);
        if (result != 0) {
            throw std::runtime_error(std::string("getaddrinfo failed with error:") + std::to_string(result));
        }
    }

    void createListenSocket() {
        ListenSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
            throw std::runtime_error(std::string("Socket creation failed") + std::to_string(WSAGetLastError()));
        }
    }

    void bindListenSocket() {
        int result = bind(ListenSocket, addrResult->ai_addr, (int) addrResult->ai_addrlen);
        if (result == SOCKET_ERROR) {
            throw std::runtime_error(std::string("Binding socket failed. Error: ") + std::to_string(WSAGetLastError()));
        }
    }

    void listenListenSocket() {
        int result = listen(ListenSocket, SOMAXCONN);
        if (result == SOCKET_ERROR) {
            throw std::runtime_error(std::string("Listening socket failed ") + std::to_string(WSAGetLastError()));
        }
    }

    bool sendAll(SOCKET socket, const char *data, int length) {
        int offset = 0;

        while (offset < length) {
            int sent = send(socket, data + offset, length - offset, 0);

            if (sent == SOCKET_ERROR) {
                cerr << "send failed: " << WSAGetLastError() << '\n';
                return false;
            }

            if (sent == 0) {
                cerr << "send made no progress\n";
                return false;
            }

            offset += sent;
        }

        return true;
    }


    void RecvFromClient(int delay_ms) {
        if (delay_ms <= 0) return;

        DWORD timeout = static_cast<DWORD>(delay_ms);
        if (setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
                       sizeof(timeout)) == SOCKET_ERROR) {
            cerr << "setsockopt failed: " << WSAGetLastError() << "\n";
            return;
        }

        int result = recv(ClientSocket, recvBuffer, static_cast<int>(sizeof(recvBuffer)), 0);

        if (result > 0) {
            cout.write(recvBuffer, result) << '\n';
        } else if (result == 0) {
            cerr << "Client disconnected...\n";
        } else {
            int error = WSAGetLastError();

            if (error == WSAETIMEDOUT) {
                cerr << "No data received within timeout\n";
            } else {
                cerr << "recv failed: " << error << '\n';
            }
        }
    }

    void startListen() {
        while (true) {
            ClientSocket = accept(ListenSocket, nullptr, nullptr);
            if (ClientSocket == INVALID_SOCKET) {
                cerr << "Accepting socket failed" << WSAGetLastError() << endl;
                continue;
            }

            if (!sendAll(ClientSocket, sendBuffer, static_cast<int>(strlen(sendBuffer)))) {
                closesocket(ClientSocket);
                ClientSocket = INVALID_SOCKET;
                continue;
            }

            cerr << "Sent: " << sendBuffer << flush;
            RecvFromClient(delayForAnswer);
            shutdown(ClientSocket, SD_SEND);
            closesocket(ClientSocket);
            ClientSocket = INVALID_SOCKET;
        }
    }
};

int main() {
    try {
        Server server;
        server.CheckAddres();
        server.createListenSocket();
        server.bindListenSocket();
        server.listenListenSocket();
        server.startListen();
    } catch (const std::exception &e) {
        cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
