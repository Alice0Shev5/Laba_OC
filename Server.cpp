#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <queue>

#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)

using namespace std;

const int MAX_CLIENTS = 2;
const int PORT = 12345;
const int BUFFER_SIZE = 1024;

struct ClientInfo {
    SOCKET socket;
    HANDLE threadHandle;
    DWORD threadId;
    int color;
    string name;
};

vector<ClientInfo> clients;
vector<string> chatHistory;
queue<SOCKET> waitingQueue;

HANDLE clientMutex;
HANDLE queueMutex;
HANDLE historyMutex;
HANDLE exitEvent;

const vector<int> COLORS = {
    FOREGROUND_RED | FOREGROUND_INTENSITY,
    FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
};


// Константы для клиентов
const int CLIENT_INSTANCES = 3; // Количество клиентов для запуска


// Запуск клиентского процесса
bool LaunchClientProcess() {
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;
    wstring cmdLine = L"Client.exe";

    if (!CreateProcess(
        NULL,
        const_cast<LPWSTR>(cmdLine.c_str()),
        NULL,
        NULL,
        FALSE,
        CREATE_NEW_CONSOLE, // 
        NULL,
        NULL,
        &si,
        &pi))
    {
        cout << "[ERROR] CreateProcess failed: " << GetLastError() << endl;
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return true;
}


DWORD WINAPI ClientHandler(LPVOID lpParam) {
    SOCKET clientSocket = (SOCKET)lpParam;
    char buffer[BUFFER_SIZE];
    int bytesReceived;
    int currentColor;
    string clientName;

    // Назначаем цвет клиенту
    WaitForSingleObject(clientMutex, INFINITE);
    currentColor = COLORS[clients.size() % COLORS.size()];
    ReleaseMutex(clientMutex);

    string colorMsg = "COLOR:" + to_string(currentColor) + "\n";
    send(clientSocket, colorMsg.c_str(), colorMsg.size() + 1, 0);

    // Отправляем историю чата новому клиенту
    WaitForSingleObject(historyMutex, INFINITE);
    for (const auto& msg : chatHistory) {
        send(clientSocket, msg.c_str(), msg.size() + 1, 0);
    }
    ReleaseMutex(historyMutex);

    // Получаем имя клиента
    bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesReceived <= 0) {
        closesocket(clientSocket);
        return 0;
    }
    clientName = string(buffer, bytesReceived);

    // Обновляем информацию о клиенте
    WaitForSingleObject(clientMutex, INFINITE);
    for (auto& client : clients) {
        if (client.socket == clientSocket) {
            client.name = clientName;
            client.color = currentColor;
            break;
        }
    }
    ReleaseMutex(clientMutex);

    // Уведомляем о новом подключении
    string joinMsg = clientName + " присоединился к чату.\n";
    WaitForSingleObject(historyMutex, INFINITE);
    chatHistory.push_back(joinMsg);
    ReleaseMutex(historyMutex);

    WaitForSingleObject(clientMutex, INFINITE);
    for (const auto& client : clients) {
        if (client.socket != clientSocket) {
            send(client.socket, joinMsg.c_str(), joinMsg.size() + 1, 0);
        }
    }
    ReleaseMutex(clientMutex);

    // Основной цикл обработки сообщений
    while (true) {
        if (WaitForSingleObject(exitEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            break;
        }

        string message(buffer, bytesReceived);
        string formattedMsg = clientName + ": " + message + "\n";

        WaitForSingleObject(historyMutex, INFINITE);
        chatHistory.push_back(formattedMsg);
        ReleaseMutex(historyMutex);

        WaitForSingleObject(clientMutex, INFINITE);
        for (const auto& client : clients) {
            if (client.socket != clientSocket) {
                send(client.socket, formattedMsg.c_str(), formattedMsg.size() + 1, 0);
            }
        }
        ReleaseMutex(clientMutex);
    }

    // Обработка отключения клиента
    string leaveMsg = clientName + " покинул чат.\n";
    WaitForSingleObject(historyMutex, INFINITE);
    chatHistory.push_back(leaveMsg);
    ReleaseMutex(historyMutex);

    WaitForSingleObject(clientMutex, INFINITE);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->socket == clientSocket) {
            clients.erase(it);
            break;
        }
    }
    for (const auto& client : clients) {
        send(client.socket, leaveMsg.c_str(), leaveMsg.size() + 1, 0);
    }

    // Добавляем следующего клиента из очереди
    WaitForSingleObject(queueMutex, INFINITE);
    if (!waitingQueue.empty() && clients.size() < MAX_CLIENTS) {
        SOCKET nextClient = waitingQueue.front();
        waitingQueue.pop();

        ClientInfo newClient;
        newClient.socket = nextClient;
        newClient.threadHandle = CreateThread(NULL, 0, ClientHandler, (LPVOID)nextClient, 0, &newClient.threadId);
        clients.push_back(newClient);

        string acceptMsg = "SERVER: Вы подключены к чату.\n";
        send(nextClient, acceptMsg.c_str(), acceptMsg.size() + 1, 0);
    }
    ReleaseMutex(queueMutex);
    ReleaseMutex(clientMutex);

    closesocket(clientSocket);
    return 0;
}

void Cleanup() {
    SetEvent(exitEvent);

    WaitForSingleObject(clientMutex, INFINITE);
    for (auto& client : clients) {
        closesocket(client.socket);
        WaitForSingleObject(client.threadHandle, INFINITE);
        CloseHandle(client.threadHandle);
    }
    clients.clear();
    ReleaseMutex(clientMutex);

    WaitForSingleObject(queueMutex, INFINITE);
    while (!waitingQueue.empty()) {
        closesocket(waitingQueue.front());
        waitingQueue.pop();
    }
    ReleaseMutex(queueMutex);

    CloseHandle(clientMutex);
    CloseHandle(queueMutex);
    CloseHandle(historyMutex);
    CloseHandle(exitEvent);
}


int main() {
    setlocale(LC_ALL, "RU");
    
    for (int i = 0; i < CLIENT_INSTANCES; i++) {
        if (!LaunchClientProcess()) {
            cerr << "Ошибка запуска клиентского процесса" << endl;
            return 1;
        }
    }
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Ошибка инициализации Winsock" << endl;
        return 1;
    }

    clientMutex = CreateMutex(NULL, FALSE, NULL);
    queueMutex = CreateMutex(NULL, FALSE, NULL);
    historyMutex = CreateMutex(NULL, FALSE, NULL);
    exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Создание сокета сервера
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        cerr << "Ошибка создания сокета" << endl;
        Cleanup();
        WSACleanup();
        return 1;
    }

    // Настройка адреса сервера
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // Привязка сокета к адресу
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Ошибка привязки сокета" << endl;
        closesocket(serverSocket);
        Cleanup();
        WSACleanup();
        return 1;
    }

    // Перевод сокета в режим прослушивания
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Ошибка прослушивания" << endl;
        closesocket(serverSocket);
        Cleanup();
        WSACleanup();
        return 1;
    }

    cout << "Сервер запущен. Ожидание подключений..." << endl;


    while (true) {
        if (WaitForSingleObject(exitEvent, 0) == WAIT_OBJECT_0) {
            break;
        }
        
        // Принимаем входящее подключение
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                cerr << "Ошибка при принятии подключения" << endl;
            }
            continue;
        }

        // Блокируем доступ к списку клиентов
        WaitForSingleObject(clientMutex, INFINITE);

        // Проверяем, не превышено ли максимальное количество клиентов
        if (clients.size() < MAX_CLIENTS) {
            ClientInfo newClient;
            newClient.socket = clientSocket;
            // Создаем поток для обработки клиента
            newClient.threadHandle = CreateThread(NULL, 0, ClientHandler, (LPVOID)clientSocket, 0, &newClient.threadId);
            clients.push_back(newClient);
            cout << "Клиент подключен. Всего клиентов: " << clients.size() << endl;
        }
        else {
            // Если достигнуто максимальное количество клиентов, добавляем сокет в очередь ожидания
            WaitForSingleObject(queueMutex, INFINITE);
            waitingQueue.push(clientSocket);
            ReleaseMutex(queueMutex);
            cout << "Клиент добавлен в очередь ожидания. Размер очереди: " << waitingQueue.size() << endl;
            string waitMsg = "SERVER: Вы в очереди. Пожалуйста, подождите...\n";
            send(clientSocket, waitMsg.c_str(), waitMsg.size() + 1, 0);
        }
        ReleaseMutex(clientMutex);
    }

    closesocket(serverSocket);
    Cleanup();
    WSACleanup();
    cout << "Сервер остановлен." << endl;
    return 0;
}