#include <iostream>
#include <string>

#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)

using namespace std;

const string SERVER_IP = "127.0.0.1";
const int PORT = 12345;
const int BUFFER_SIZE = 1024;

HANDLE hConsole;
HANDLE exitEvent;
int textColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;


DWORD WINAPI ReceiveThread(LPVOID lpParam) {
    SOCKET sock = *(SOCKET*)lpParam;
    char buffer[BUFFER_SIZE];
    int bytesReceived;

    while (true) {
        if (WaitForSingleObject(exitEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        bytesReceived = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            cout << "Соединение с сервером потеряно." << endl;
            SetEvent(exitEvent);
            break;
        }

        string message(buffer, bytesReceived);

        if (message.find("COLOR:") == 0) {
            textColor = stoi(message.substr(6));
            SetConsoleTextAttribute(hConsole, textColor);
        }
        else {
            SetConsoleTextAttribute(hConsole, textColor);
            cout << message;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }
    }
    return 0;
}


int main() {
    Sleep(2000);
    setlocale(LC_ALL, "RU");
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Ошибка инициализации Winsock" << endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета" << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP.c_str());
    serverAddr.sin_port = htons(PORT);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Ошибка подключения к серверу" << endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    cout << "Подключено к серверу. Введите ваше имя: ";
    string name;
    getline(cin, name);
    send(sock, name.c_str(), name.size() + 1, 0);

    HANDLE receiveThread = CreateThread(NULL, 0, ReceiveThread, &sock, 0, NULL);

    cout << "Теперь вы можете отправлять сообщения. Введите 'exit' для выхода." << endl;

    while (true) {
        string message;
        getline(cin, message);

        if (message == "exit") {
            SetEvent(exitEvent);
            break;
        }

        if (send(sock, message.c_str(), message.size() + 1, 0) == SOCKET_ERROR) {
            cerr << "Ошибка отправки сообщения" << endl;
            SetEvent(exitEvent);
            break;
        }
    }

    WaitForSingleObject(receiveThread, INFINITE);
    CloseHandle(receiveThread);

    closesocket(sock);
    CloseHandle(exitEvent);
    WSACleanup();
    return 0;
}