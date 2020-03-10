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
#include <unordered_map>
#include "http_parser.h"

#pragma comment(lib,"ws2_32.lib")

class HttpContext;
class IoContext;
class SocketContext;
class SocketHttpContext;
class Http;
class Tcp;

typedef int(*http_data_cb) (http_parser*, const char *at, size_t length);
typedef int(*http_cb) (http_parser*);
typedef void(*void_function);

using namespace std;

//-------------------Contexts-----------------------

class HttpContext {
public:
	OVERLAPPED overLapped;
	HANDLE fileHandle;
	enum ioType {
		processing,
		done,
	};
	ioType type;
};

class IoContext {
public:

	IoContext();
	~IoContext();
	void resetWsaBuf();
	void resetOverLapped();

	SOCKET s;
	OVERLAPPED overLapped;
	WSABUF wsaBuf;
	DWORD numberOfBytes;
	DWORD flags;
	SOCKADDR_IN sockAddr;

	long mark;
};

class SocketContext {
public:

	IoContext * ioContext;

	CRITICAL_SECTION lock;

	SocketContext();
	~SocketContext();

	enum ioType {
		idle,
		accept,
		recv,
		send,
	};
	//mark the current io status
	ioType type = ioType::idle;
};

class SocketHttpContext {
public:
	SocketContext * socketContext;
	HttpContext * httpContext;
};

//-------------------------Threads-----------------------

class ThreadForTcp {
public:
	static int threadNum;
	int currentNum;
	static CRITICAL_SECTION lock;

	ThreadForTcp();
	ThreadForTcp(LPVOID lpParam);
	~ThreadForTcp();

	static DWORD WINAPI run(LPVOID lpParam);
};

class ThreadForHttp {
public:
	static int threadNum;
	int currentNum;
	static CRITICAL_SECTION lock;

	ThreadForHttp();
	ThreadForHttp(LPVOID lpParam);
	~ThreadForHttp();

	static DWORD WINAPI run(LPVOID lpParam);
};

//-------------------------------Http-----------------------

class Http {
public:
	unordered_map<long, void_function>http_data_cbs;
	unordered_map<long, void_function> http_cbs;

	http_data_cb custom_url_cb;
	http_data_cb custom_header_field_cb;
	http_data_cb custom_header_value_cb;

	http_parser * parser;
	http_parser_settings settings;

	HANDLE completionPort;

	Http();
	~Http();

	void init();
	void start();

	long calculateCallback(char name[], int len);
	void insertCallback(char name[], int len, void_function);
	void insertDataCallback(char name[], int len, void_function);

	//------Hello world-----
	void dealRecvHelloWorld(SocketHttpContext * shc);
	void dealSendHelloWorld(SocketHttpContext * shc);

	//------DEAL--------
	void dealRecv(SocketHttpContext * shc);
	void dealSend(SocketHttpContext * shc);
	//------POST--------
	void postRecv(SocketHttpContext * shc, HttpContext * hc);
	void postSend(SocketHttpContext * shc, HttpContext * hc);
};

//---------------------------Tcp----------------------------

class Tcp {
public:
	friend class ThreadForTcp;
	friend class ThreadForHttp;

	SocketContext * listenSocketContext;
	HANDLE completionPort;

	LPFN_ACCEPTEX fnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS fnGetAcceptExSockAddrs;

	Http * http;

public:
	Tcp();
	~Tcp();
	//0.post accept
	//1.run thread
	void start(int i = 10);
	void destory();

	//1.init wsa service
	//2.create iocp
	//3.bind and listen
	bool init();

	bool postAccept(SocketContext * sc, IoContext * ic);

	bool dealAccept(SocketContext * sc, IoContext * ic);

	bool postRecv(SocketContext * sc, IoContext * ic);

	bool dealRecv(SocketContext * sc, IoContext * ic);

	bool postSend(SocketContext * sc, IoContext * ic);

	bool dealSend(SocketContext * sc, IoContext * ic);

	//check if the connection is alive
	bool isAlive(SocketContext * sc, IoContext * ic);

	//-----------------------------------------------------------------
};