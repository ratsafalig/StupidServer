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

#define HTTP_RESPONSE_HEAD \
"HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Date: Thu, 12 Mar 2020 12:55:10 GMT\r\n\
Connection: keep - alive\r\n\
Transfer-Encoding: chunked\r\n\
\r\n"
#define HTTP_RESPONSE_TAIL \
"\r\n\
0\r\n\
\r\n"
#define HTTP_MAX_RESPONSE_SIZE 10000
//--------------------
#define FILE_MAX_BUFFER_SIZE 10000
#define FILE_MAX_NAME_SIZE 100
//--------------------
#define URL_TYPE_MAP_ONLY 0
#define URL_TYPE_CB_ONLY 1
//--------------------

class HttpContext;
class IoContext;
class SocketContext;
class SocketHttpContext;
class Http;
class Tcp;

typedef int(*http_data_cb) (http_parser*, const char *at, size_t length);
typedef int(*http_cb) (http_parser*);

using namespace std;

//----------Utility----------

//----------Contexts----------

class HttpContext {
public:
	OVERLAPPED overLapped;
	HANDLE fileHandle;
	DWORD numberOfBytes;
	enum ioType {
		processing,
		done,
	};
	ioType type;

	HttpContext();
	~HttpContext();
	void resetOverLapped();
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
	SocketHttpContext();
	~SocketHttpContext();
	SocketContext * socketContext;
	HttpContext * httpContext;
};

//----------Threads----------

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

//----------Http----------

class Http {
public:
	CRITICAL_SECTION lock;

	unordered_map<size_t, pair<char*, size_t>> mappings;

	http_parser * parser;
	http_parser_settings settings;

	//completion for exchange completion between Http and Tcp class
	HANDLE exchangeCompletionPort;
	HANDLE httpCompletionPort;

	struct custom_data {
		int type = 0;
		Http * http = NULL;
		SocketHttpContext * shc = NULL;
		void * cb;
		void * param;
	};


	Http();
	~Http();

	void init();
	void start();

	//----------Hello world----------
	void dealRecvHelloWorld(SocketHttpContext * shc);

	//----------Deal----------
	void dealRecv(SocketHttpContext * shc);
	void dealSend(SocketHttpContext * shc);

	void map(char * url, size_t url_len, char * file, size_t file_len);

};

//----------Tcp----------

class Tcp {
public:
	friend class ThreadForTcp;
	friend class ThreadForHttp;

	SocketContext * listenSocketContext;
	HANDLE tcpCompletionPort;

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

	bool dealAccept(SocketContext * sc, IoContext * ic);

	bool dealRecv(SocketContext * sc, IoContext * ic);

	bool dealSend(SocketContext * sc, IoContext * ic);

	bool postSend(SocketContext * sc, IoContext * ic);
	bool postRecv(SocketContext * sc, IoContext * ic);
	bool postAccept(SocketContext * sc, IoContext * ic);

	//check if the connection is alive
	bool isAlive(SocketContext * sc, IoContext * ic);

	//-----------------------------------------------------------------
};