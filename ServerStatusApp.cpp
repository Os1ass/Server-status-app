#include "ServerStatusApp.h"
#include <iostream>

const LPCWSTR         g_pipeName = L"\\\\.\\pipe\\ServerStatusPipe";
const BYTE			  g_magicNumber[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
const std::string	  g_magicNumberString(reinterpret_cast<const char*>(g_magicNumber), sizeof(g_magicNumber));
HWND				  g_hEditRecv;
HANDLE				  g_pipeThread = INVALID_HANDLE_VALUE;
HANDLE				  g_stopEvent = INVALID_HANDLE_VALUE;
HANDLE				  g_hPipe = INVALID_HANDLE_VALUE;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	setlocale(LC_ALL, "RUSSIAN");
	const wchar_t CLASS_NAME[] = L"ServerStatusWindowClass";
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(
		0,
		CLASS_NAME,
		L"Server status",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		500, 400,
		NULL, NULL,
		hInstance, NULL
	);

	if (hwnd == NULL)
	{
		return 0;
	}

	ShowWindow(hwnd, nCmdShow);
	SetWindowText(g_hEditRecv, L"Searching for server...\r\n");
	g_stopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_pipeThread = CreateThread(NULL, 0, LPTHREAD_START_ROUTINE(PipeHandle), NULL, 0, NULL);

	MSG msg = { };
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        g_hEditRecv = CreateWindow(L"EDIT", NULL,
            WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            10, 10, 460, 330, hwnd, NULL, NULL, NULL);
    }
    return 0;

    case WM_DESTROY:
		if (g_hPipe != INVALID_HANDLE_VALUE)
		{
			SetEvent(g_stopEvent);
			if (WaitForSingleObject(g_pipeThread, 1000) != WAIT_OBJECT_0)
			{
				TerminateProcess(g_pipeThread, 1);
			}
			CloseHandle(g_pipeThread);
			CloseHandle(g_stopEvent);
			CloseHandle(g_hPipe);
		}
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void ProcessPipeConnection(LPOVERLAPPED overlap)
{
	BOOL fSuccess;
	char buffer[BUFFER_SIZE];
	buffer[0] = '\0';
	int clientsCnt;
	std::string clientsStr;

	while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0)
	{
		ResetEvent(overlap->hEvent);
		fSuccess = ReadFile(
			g_hPipe,
			buffer,
			BUFFER_SIZE,
			NULL,
			overlap
		);

		if (GetLastError() == ERROR_PIPE_NOT_CONNECTED)
		{
			Sleep(500);
			continue;
		}
		if (!fSuccess && GetLastError() != ERROR_IO_PENDING)
		{
			OutputDebugString(L"Read file failed");
			SetWindowText(g_hEditRecv, L"Ooops... some error.");
			return;
		}

		while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0 &&
			WaitForSingleObject(overlap->hEvent, 0) != WAIT_OBJECT_0)
		{
			Sleep(500);
		}
		if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0)
		{
			break;
		}
		
		clientsStr = ParseClients(buffer, clientsCnt);
		PrintClients(g_hEditRecv, clientsStr, clientsCnt);
		Sleep(1000);
	}
}

DWORD WINAPI PipeHandle(LPVOID lpParam)
{
	do {
		g_hPipe = CreateFile(
			g_pipeName,
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_READONLY |
			FILE_FLAG_OVERLAPPED,
			NULL
		);
	} while ((GetLastError() == ERROR_PIPE_BUSY ||
			 GetLastError() == ERROR_FILE_NOT_FOUND) &&
			 WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0);

	if (g_hPipe == INVALID_HANDLE_VALUE)
	{
		OutputDebugString(L"Create file failed");
		SetWindowText(g_hEditRecv, L"Ooops... some error.");
		return GetLastError();
	}

	if (WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0)
	{
		CloseHandle(g_hPipe);
		return 0;
	}

	OVERLAPPED overlap;
	overlap.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
	overlap.Offset = 0;
	overlap.OffsetHigh = 0;

	if (overlap.hEvent == INVALID_HANDLE_VALUE)
	{
		OutputDebugString(L"Create event failed");
		CloseHandle(g_hPipe);
		SetWindowText(g_hEditRecv, L"Ooops... some error.");
		return GetLastError();
	}

	ProcessPipeConnection(&overlap);
	CloseHandle(overlap.hEvent);
	CloseHandle(g_hPipe);
	return 0;
}

void PrintClients(HWND hwnd, std::string text, int clientsCnt)
{
	auto now = std::chrono::system_clock::now();
	std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

	char nowTimeBuffer[TIME_STRING_LENGTH];
	ctime_s(nowTimeBuffer, TIME_STRING_LENGTH, &nowTime);

	std::string timeNow = std::string(nowTimeBuffer, TIME_STRING_LENGTH - 2);
	if (clientsCnt == -1)
	{
		text = timeNow + "\r\nLost message from server\0";
	}
	else
	{
		text = timeNow + "\r\nConnected " + std::to_string(clientsCnt) + " users:\r\n" + text + '\0';
	}

	size_t textSize;
	wchar_t* wtext = new wchar_t[BUFFER_SIZE];
	mbstowcs_s(&textSize, wtext, BUFFER_SIZE, text.c_str(), _TRUNCATE);
	
	SetWindowText(hwnd, wtext);
	delete[] wtext;
}

std::string ParseClients(char* buffer, int& clientsCnt)
{
	clientsCnt = 0;
	int bufferSize = strlen(buffer);
	std::string bufferStr(buffer, bufferSize);
	//if len < 2 * separatorLen or no separator in begin or end
	if (bufferSize < g_magicNumberString.length() * 2 ||
		(bufferStr.substr(0, 4) != g_magicNumberString ||
		bufferStr.substr(bufferSize - 4, 4) != g_magicNumberString))
	{
		clientsCnt = -1;
		return "";
	}

	if (bufferSize == g_magicNumberString.length() * 2)
	{
		return "";
	}

	std::string clients;
	int lastSeparator = 0, curSeparator;
	while (true)
	{
		curSeparator = bufferStr.find(g_magicNumberString, lastSeparator + 1);
		if (curSeparator == bufferStr.npos)
		{
			break;
		}

		if (lastSeparator < curSeparator - 4)
		{
			clients += bufferStr.substr(lastSeparator + 4, curSeparator - lastSeparator - 4) + "\r\n";
			clientsCnt++;
		}
		lastSeparator = curSeparator;
	}

	return clients;
}