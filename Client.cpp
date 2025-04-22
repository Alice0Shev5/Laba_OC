#include <windows.h>
#include <iostream>
#include <string>
#define BUFSIZE 512
using namespace std;

void print_time() {
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    printf("%02d:%02d:%02d.%03d\t", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
}

int main(int argc, char* argv[])
{
    Sleep(2000);
    // Парсинг аргументов командной строки
    if (argc < 2) {
        print_time();
        cout << "[ERROR] Lifetime parameter not specified\n";
        return -1;
    }

    int lifetime = 0;
    try {
        lifetime = stoi(argv[1]);
    }
    catch (...) {
        print_time();
        cout << "[ERROR] Invalid lifetime parameter\n";
        return -1;
    }

    print_time();
    cout << "[CLIENT] Started with lifetime: " << (lifetime == 0 ? "infinite" : to_string(lifetime) + " seconds") << '\n';

    HANDLE hPipe;
    DWORD cbIO;
    const int CONFIRMATION = 1; // Код подтверждения для сервера

    // Подключение к серверу
    while (true) {
        hPipe = CreateFile(
            L"\\\\.\\pipe\\mynamedpipe",
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hPipe != INVALID_HANDLE_VALUE)
            break;

        DWORD dwError = GetLastError();
        if (dwError != ERROR_PIPE_BUSY) {
            print_time();
            cout << "[ERROR] Could not open pipe. GLE=" << dwError << '\n';
            return -1;
        }

        if (!WaitNamedPipe(L"\\\\.\\pipe\\mynamedpipe", 20000)) {
            print_time();
            cout << "[ERROR] Could not open pipe: 20 second wait timed out.\n";
            return -1;
        }
    }

    // Установка режима чтения для канала
    DWORD dwMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &dwMode, NULL, NULL)) {
        print_time();
        cout << "[ERROR] SetNamedPipeHandleState failed. GLE=" << GetLastError() << '\n';
        CloseHandle(hPipe);
        return -1;
    }

    // Отправка подтверждения серверу
    if (!WriteFile(hPipe, &CONFIRMATION, sizeof(CONFIRMATION), &cbIO, NULL)) {
        print_time();
        cout << "[ERROR] WriteFile to pipe failed. GLE=" << GetLastError() << '\n';
        CloseHandle(hPipe);
        return -1;
    }

    
    print_time();
    cout << "[CLIENT] Confirmation sent to server\n";

    // Основная работа клиента
    if (lifetime == 0) {
        // Бесконечный режим работы
        print_time();
        cout << "[CLIENT] Running in infinite mode\n";
        while (true) {
            // Полезная работа клиента
            Sleep(1000);

            // Проверка соединения с сервером
            if (!WriteFile(hPipe, &CONFIRMATION, sizeof(CONFIRMATION), &cbIO, NULL)) {
                print_time();
                    cout << "[ERROR] Connection with server lost\n";
                    break;
            }
        }
    }
    else {
        // Режим с ограниченным временем жизни
        print_time();
            cout << "[CLIENT] Running for " << lifetime << " seconds\n";

            DWORD startTime = GetTickCount();
        while ((GetTickCount() - startTime) < (DWORD)(lifetime * 1000)) {
            // Полезная работа клиента
            Sleep(1000);

            // Периодическая проверка соединения
            if (!WriteFile(hPipe, &CONFIRMATION, sizeof(CONFIRMATION), &cbIO, NULL)) {
                print_time();
                cout << "[ERROR] Connection with server lost\n";
                break;
            }

            // Вывод оставшегося времени каждые 5 секунд
            if (((GetTickCount() - startTime) / 1000) % 5 == 0) {
                print_time();
                cout << "[CLIENT] Time remaining: "
                    << (lifetime - (GetTickCount() - startTime) / 1000)
                    << " seconds\n";
            }
        }
    }

    print_time();
    cout << "[CLIENT] Work completed. Exiting...\n";
    CloseHandle(hPipe);
    return 0;
}