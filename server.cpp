#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstring>

using namespace std;

class Server {
private:
    WSADATA wsaData;
    ADDRINFO hints;
    ADDRINFO *addrResult = nullptr;
    SOCKET ClientSocket = INVALID_SOCKET;
    SOCKET ListenSocket = INVALID_SOCKET;

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
            cout << "WSAStartup failed, result = " << result << endl;
            throw std::runtime_error(std::string("WSAStartup failed with error: ") + std::to_string(result));
        }
    }

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    ~Server() {
        addrResult = nullptr;
        WSACleanup();
        shutdown(ClientSocket, SD_SEND);
        closesocket(ClientSocket);
        ClientSocket = INVALID_SOCKET;
    }


    void CheckAddres() {
        int result = getaddrinfo("localhost", "8080", &hints, &addrResult);
        if (result != 0) {
            WSACleanup();
            throw std::runtime_error(std::string("getaddrinfo failed with error:") + std::to_string(result));
        }
    }

    void createListenSocket() {
        ListenSocket = socket(addrResult->ai_family, addrResult->ai_socktype, addrResult->ai_protocol);
        if (ListenSocket == INVALID_SOCKET) {
            int error = WSAGetLastError();
            freeaddrinfo(addrResult);
            WSACleanup();
            throw std::runtime_error(std::string("Socket creation failed") + std::to_string(error));
        }
    }

    void bindListenSocket() {
        int result = bind(ListenSocket, addrResult->ai_addr, (int) addrResult->ai_addrlen);
        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            closesocket(ListenSocket);
            ListenSocket = INVALID_SOCKET;
            freeaddrinfo(addrResult);
            addrResult = nullptr;
            WSACleanup();
            throw std::runtime_error(std::string("Binding socket failed. Error: ") + std::to_string(error));
        }
    }

    void listenListenSocket() {
        int result = listen(ListenSocket, SOMAXCONN);
        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            closesocket(ListenSocket);
            freeaddrinfo(addrResult);
            WSACleanup();
            throw std::runtime_error(std::string("Listening socket failed ") + std::to_string(error));
        }
    }

    bool sendAll(SOCKET socket, const char* data, int length) {
        int offset = 0;

        while (offset < length) {
            int sent = send(socket, data + offset, length - offset, 0);

            if (sent == SOCKET_ERROR) {
                int error = WSAGetLastError();
                throw std::runtime_error(string("send failed: ") + to_string(error));
                return false;
            }

            if (sent == 0) {
                std::cerr << "send made no progress\n";
                return false;
            }

            offset += sent;
        }

        return true;
    }

    void startListen() {
        while (true) {
            ClientSocket = accept(ListenSocket, nullptr, nullptr);
            if (ClientSocket == INVALID_SOCKET) {
                cout << "Accepting socket failed" << endl;
                continue;
            }

            if (!sendAll(ClientSocket, sendBuffer, static_cast<int>(strlen(sendBuffer)))) {
                closesocket(ClientSocket);
                ClientSocket = INVALID_SOCKET;
                continue;
            }

            cout << "Sent: " << sendBuffer << flush;

            ZeroMemory(recvBuffer, 512);
            int result = recv(ClientSocket, recvBuffer, 512, 0);
            if (result > 0) {
                cout.write(recvBuffer, result) << '\n';
            } else if (result == 0) {
                cout << "Client disconnected..." << endl;
            } else {
                cout << "recv failed:" << WSAGetLastError() << endl;
            };

            shutdown(ClientSocket, SD_SEND);
            closesocket(ClientSocket);
            ClientSocket = INVALID_SOCKET;
        }
    }
};

int main() {
    Server server;
    server.CheckAddres();
    server.createListenSocket();
    server.bindListenSocket();
    server.listenListenSocket();
    server.startListen();

    return 0;
}
