#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <stdexcept>

using namespace std;

class Client {
private:
    WSADATA wsaData;
    ADDRINFO hints;
    bool wsaInitialized = false;
    ADDRINFO* addrResult = nullptr;
    SOCKET ConnectSocket = INVALID_SOCKET;
    const char* sendBuffer = "OK\n";
    char recvBuffer[512];

public:
    //конструктор создания сокета и его настройка
    Client(){
        ZeroMemory(&hints,sizeof(hints));
        hints.ai_family = AF_INET; //  ipv4
        hints.ai_socktype = SOCK_STREAM; // сокет
        hints.ai_protocol = IPPROTO_TCP; // протокол tcp

        //создание сокета

        int result = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (result != 0) {
            cerr << "WSAStartup failed, result = " << result << endl;
            throw std::runtime_error(std::string("WSAStartup failed with error: ") + std::to_string(result));
        } else {
            wsaInitialized = true;
        }
    };
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    ~Client() noexcept{
        ClosingSocket();
    }

    void CheckAddres() {
        int result = getaddrinfo("localhost","8080", &hints, &addrResult);
        if (result != 0) {
            throw std::runtime_error(std::string("getaddrinfo failed with error:") + std::to_string(result));
        }
    }

    void createConnectSocket() {
        ConnectSocket = socket(addrResult->ai_family,addrResult->ai_socktype, addrResult->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            throw std::runtime_error(std::string("Socket creation failed") + std::to_string(WSAGetLastError()));
        }
    }

    void Connect() {
        int result = connect(ConnectSocket, addrResult->ai_addr, (int)addrResult->ai_addrlen);
        if (result == SOCKET_ERROR) {
            throw std::runtime_error(std::string("Unable connect to server. Error: ") + std::to_string(WSAGetLastError()));
        }
    }

    void workWithServer() {
        int result;
        do {
            ZeroMemory(recvBuffer,512);
            result = recv(ConnectSocket, recvBuffer, 512,0);
            if (result == SOCKET_ERROR) {
                int error = WSAGetLastError();
                throw std::runtime_error(std::string("Recv error ") + to_string(error));
            }

            if (result == 0) {
                throw std::runtime_error("Connection closed before receiving data");
            }

            if (result > 0) {
                std::cerr << "Received " << result << " bytes" << std::endl;
                std::cerr << "Received data: ";
                std::cerr.write(recvBuffer, result);
                std::cerr << std::flush;

                if (std::string(recvBuffer, result).find('\n') != std::string::npos) {
                    break;
                }
            }
        } while (result > 0);
    }

    void ClosingSocket() {
        if (ConnectSocket != INVALID_SOCKET) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
        }

        if (addrResult != nullptr) {
            freeaddrinfo(addrResult);
            addrResult = nullptr;
        }

        if (wsaInitialized) {
            WSACleanup();
            wsaInitialized = false;
        }
    }
};

int main() {
    try {
        Client client;
        client.CheckAddres();
        client.createConnectSocket();
        client.Connect();
        client.workWithServer();
    } catch (const exception& e) {
        cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}