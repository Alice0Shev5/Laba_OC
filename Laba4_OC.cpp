#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <cstdlib>
#include <signal.h>

#include <conio.h>

#define CONNECTING_STATE 0
#define READING_STATE 1
#define WRITING_STATE 2

#define INSTANCES 3        // Минимум 3 клиента
#define PIPE_TIMEOUT 5000
#define BUFSIZE 512

using namespace std;

// Структура для хранения информации о клиенте
typedef struct {
    OVERLAPPED oOverlap;
    HANDLE hPipeInst;
    unsigned int client_lifetime; // Время жизни клиента в секундах (0 - бесконечно)
    DWORD dwState;
    bool fPendingIO;
} PIPEINST;

// Глобальные переменные
PIPEINST Pipe[INSTANCES];
HANDLE hEvents[INSTANCES];
bool g_bRunning = true;

// Прототипы функций
void DisconnectAndReconnect(unsigned int);
bool ConnectToNewClient(HANDLE, LPOVERLAPPED);
bool LaunchClientProcess(unsigned int lifetime);
void GenerateClientLifetimes(vector<unsigned int>& lifetimes);
void print_time();
void CheckForKeyboardInput();

// Функция для вывода времени
void print_time() {
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    printf("%02d:%02d:%02d.%03d\t", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
}


// Генерация случайного времени жизни для клиентов
void GenerateClientLifetimes(vector<unsigned int>& lifetimes) {
    srand(static_cast<unsigned>(time(nullptr)));
    for (int i = 0; i < INSTANCES; ++i) {
        // 30% chance for infinite lifetime (0), otherwise 5-15 seconds
        lifetimes.push_back((rand() % 10 < 3) ? 0 : 5 + rand() % 13);
    }
}

// Запуск клиентского процесса
bool LaunchClientProcess(unsigned int lifetime) {
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;
    wstring cmdLine = L"Client.exe " + to_wstring(lifetime);

    if (!CreateProcess(
        NULL,
        const_cast<LPWSTR>(cmdLine.c_str()),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi))
    {
        print_time();
        wcout << L"[ERROR] CreateProcess failed: " << GetLastError() << endl;
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    print_time();
    wcout << L"[SERVER] Launched client with lifetime: " << lifetime
        << (lifetime == 0 ? L" (infinite)" : L" seconds") << endl;
    return true;
}

// Подключение нового клиента
bool ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo) {
    BOOL fConnected = ConnectNamedPipe(hPipe, lpo);

    if (fConnected) {
        print_time();
        cout << "[ERROR] ConnectNamedPipe failed with " << GetLastError() << endl;
        return false;
    }

    switch (GetLastError()) {
    case ERROR_IO_PENDING:
        return true;
    case ERROR_PIPE_CONNECTED:
        if (SetEvent(lpo->hEvent)) {
            print_time();
            cout << "[SERVER] Client connected immediately\n";
            return false;
        }
    default:
        print_time();
        cout << "[ERROR] ConnectNamedPipe failed with " << GetLastError() << endl;
        return false;
    }
}

// Отключение и переподключение клиента
void DisconnectAndReconnect(unsigned int i) {
    if (!DisconnectNamedPipe(Pipe[i].hPipeInst)) {
        print_time();
        cout << "[ERROR] DisconnectNamedPipe failed: " << GetLastError() << endl;
    }

    Pipe[i].fPendingIO = ConnectToNewClient(Pipe[i].hPipeInst, &Pipe[i].oOverlap);
    Pipe[i].dwState = Pipe[i].fPendingIO ? CONNECTING_STATE : READING_STATE;
}


// Проверка нажатия клавиши
void CheckForKeyboardInput() {
    if (_kbhit()) {
        _getch(); // Считываем нажатую клавишу
        g_bRunning = false;
        print_time();
        cout << "[SERVER] Key pressed. Shutting down...\n";
    }
}


int main() {

    // Генерация времени жизни для клиентов
    vector<unsigned int> clientLifetimes;
    GenerateClientLifetimes(clientLifetimes);

    // Инициализация каналов
    for (unsigned int i = 0; i < INSTANCES; i++) {
        hEvents[i] = CreateEvent(NULL, TRUE, TRUE, NULL);
        if (hEvents[i] == NULL) {
            print_time();
            cout << "[ERROR] CreateEvent failed: " << GetLastError() << endl;
            return 1;
        }

        Pipe[i].oOverlap.hEvent = hEvents[i];
        Pipe[i].client_lifetime = clientLifetimes[i];
        Pipe[i].hPipeInst = CreateNamedPipe(
            L"\\\\.\\pipe\\mynamedpipe",
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            INSTANCES,
            BUFSIZE,
            BUFSIZE,
            PIPE_TIMEOUT,
            NULL);

        if (Pipe[i].hPipeInst == INVALID_HANDLE_VALUE) {
            print_time();
            cout << "[ERROR] CreateNamedPipe failed: " << GetLastError() << endl;
            return 1;
        }

        Pipe[i].fPendingIO = ConnectToNewClient(Pipe[i].hPipeInst, &Pipe[i].oOverlap);
        Pipe[i].dwState = Pipe[i].fPendingIO ? CONNECTING_STATE : READING_STATE;

        // Запускаем клиентский процесс с заданным временем жизни
        if (!LaunchClientProcess(Pipe[i].client_lifetime)) {
            return 1;
        }
    }

    print_time();
    cout << "[SERVER] Ready for connections\n";

    // Основной цикл сервера
    while (g_bRunning) {

        // Проверяем нажатие клавиши
        CheckForKeyboardInput();
        if (!g_bRunning) break;

        DWORD dwWait = WaitForMultipleObjects(INSTANCES, hEvents, FALSE, INFINITE);

        unsigned int i = dwWait - WAIT_OBJECT_0;
        if (i >= INSTANCES) {
            print_time();
            cout << "[ERROR] Index out of range\n";
            continue;
        }

        if (Pipe[i].fPendingIO) {
            DWORD cbRet;
            BOOL fSuccess = GetOverlappedResult(Pipe[i].hPipeInst, &Pipe[i].oOverlap, &cbRet, FALSE);

            switch (Pipe[i].dwState) {
            case CONNECTING_STATE:
                if (fSuccess) {
                    print_time();
                    cout << "[SERVER] Client " << i << " connected\n";
                    Pipe[i].dwState = READING_STATE;
                }
                break;

            case READING_STATE: {
                // Чтение подтверждения от клиента
                int clientResponse;
                DWORD bytesRead;

                if (ReadFile(Pipe[i].hPipeInst, &clientResponse, sizeof(clientResponse), &bytesRead, &Pipe[i].oOverlap)) {
                    if (clientResponse == 1) {
                        print_time();
                        cout << "[SERVER] Client " << i << " successfully started\n";
                    }
                    else {
                        print_time();
                        cout << "[WARNING] Client " << i << " reported error\n";
                    }
                }
                break;
            }
            }
        }
    }

    // Корректное завершение работы
    for (unsigned int i = 0; i < INSTANCES; i++) {
        if (Pipe[i].hPipeInst != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(Pipe[i].hPipeInst);
            CloseHandle(Pipe[i].hPipeInst);
        }
        CloseHandle(hEvents[i]);
    }

    print_time();
    cout << "[SERVER] Shutdown complete\n";
    Sleep(10000);
    return 0;
}