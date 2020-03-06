#pragma once

#include "stdafx.h"
#include "stdio.h"
#include "iostream"
#include <winsock2.h>
#include <windows.h>
#include <synchapi.h>
#include <minwinbase.h>
#include <atlstr.h>
#include <atltime.h>
#include <locale.h>
#include <strsafe.h>
#include <atlconv.h>
#include <mstcpip.h>
#include <WinSock2.h>
#include <Windows.h>
#include <MSWSock.h>
#include <vector>
#include <list>
#include <string>
#include <atltrace.h>

#pragma comment(lib,"ws2_32.lib")

using namespace std;

class Iocp;

class SocketContext;

class Thread {
public:
	static int threadNum;
	int currentNum;
	static CRITICAL_SECTION lock;

	Thread();
	Thread(LPVOID lpParam);
	static DWORD WINAPI run(LPVOID lpParam);
};
