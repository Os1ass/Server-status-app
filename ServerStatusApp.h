#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

#define BUFFER_SIZE 4096
#define TIME_STRING_LENGTH 26

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI PipeHandle(LPVOID lpParam);
std::string ParseClients(char* buffer, int& clientsCnt);
void PrintClients(HWND hwnd, std::string text, int clientsCnt);