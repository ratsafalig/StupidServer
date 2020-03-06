#include "stdafx.h"
#include "server.h"

//listen port
#define PORT 10240
//wsabuf.buf size
#define BUFSIZE (1024*4)
//IoContext's guid
int ID = 0;

class IoContext {
public:

	IoContext() : mark(ID++){
		//alloc buffer
		wsaBuf.buf = (char *)::HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BUFSIZE);
		wsaBuf.len = BUFSIZE;
		//init overlapped
		ZeroMemory(&overLapped, sizeof(overLapped));
		//init listen socket
		s = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	}

	void resetWsaBuf(){
		if(wsaBuf.buf != NULL)
			//keep in mind there is NOT &wasBuf.buf
			ZeroMemory(wsaBuf.buf, BUFSIZE);
		else
			wsaBuf.buf = (char *)::HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BUFSIZE);
	}

	void resetOverLapped() {
		ZeroMemory(&overLapped, sizeof(overLapped));
	}

	SOCKET s;
	OVERLAPPED overLapped;
	WSABUF wsaBuf;

	long mark;
};

class SocketContext{
public:

	IoContext * ioContext;

	CRITICAL_SECTION lock;

	enum ioType {
		idle,
		accept,
		recv,
		send,
	};
	//mark the current io status
	ioType type = ioType::idle;

	SocketContext(){
		InitializeCriticalSection(&lock);

		EnterCriticalSection(&lock);

		ioContext = new IoContext();

		LeaveCriticalSection(&lock);
	}
};

class Iocp {
private:
	friend class Thread;

	SocketContext * listenSocketContext;
	HANDLE completionPort;

	LPFN_ACCEPTEX fnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS fnGetAcceptExSockAddrs;

public:
	Iocp() {
	}

	//0.post accept
	//1.run thread
	void start(int i = 10) {
		DWORD dw = 0;
		IoContext * acceptIoContext = NULL;

		for (int j = 0; j < i; j++) {
			acceptIoContext = new IoContext();
			postAccept(listenSocketContext, acceptIoContext);
		}

		for (int j = 0; j < i; j++) {
			Thread * t = new Thread(this);
		}
	}

	//1.init wsa service
	//2.create iocp
	//3.bind and listen
	bool init() {
		//init wsa service
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);

		//create iocp
		completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);

		listenSocketContext = new SocketContext();
		listenSocketContext->type = SocketContext::ioType::accept;
		//get AcceptEx and GetAcceptSockAddrs
		GUID guidAcceptEx = WSAID_ACCEPTEX;
		GUID guidGetAcceptSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
		DWORD dw = 0;
		if (SOCKET_ERROR == WSAIoctl(listenSocketContext->ioContext->s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &fnAcceptEx, sizeof(fnAcceptEx), &dw, NULL, NULL))
			return false;
		if (SOCKET_ERROR == WSAIoctl(listenSocketContext->ioContext->s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidGetAcceptSockAddrs, sizeof(guidGetAcceptSockAddrs), &fnGetAcceptExSockAddrs, sizeof(fnGetAcceptExSockAddrs), &dw, NULL, NULL))
			return false;
		if (INVALID_SOCKET == listenSocketContext->ioContext->s)
			return false;
		//associate listen socket with iocp
		if (NULL == CreateIoCompletionPort((HANDLE)listenSocketContext->ioContext->s, completionPort, (ULONG_PTR)listenSocketContext, NULL))
			return false;
		//bind and listen
		sockaddr_in si;
		ZeroMemory((char *)&si, sizeof(si));
		si.sin_family = AF_INET;
		si.sin_addr.s_addr = htonl(INADDR_ANY);
		si.sin_port = htons(PORT);
		if (SOCKET_ERROR == bind(listenSocketContext->ioContext->s, (sockaddr *)&si, sizeof(si)))
			return false;
		if (SOCKET_ERROR == listen(listenSocketContext->ioContext->s, SOMAXCONN))
			return false;
		return true;
	}


	bool postAccept(SocketContext * sc, IoContext * ic) {
		if (sc == NULL || ic == NULL)
			return false;
		DWORD dw = 0;
		fnAcceptEx(sc->ioContext->s, ic->s, ic->wsaBuf.buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &dw, &ic->overLapped);
		return true;
	}

	bool postRecv(SocketContext * sc, IoContext * ic) {
		if (sc == NULL || ic == NULL)
			return false;

		ic->resetOverLapped();
		ic->resetWsaBuf();

		//config IoContext
		tcp_keepalive aliveIn;
		tcp_keepalive aliveOut;
		aliveIn.onoff = TRUE;
		aliveIn.keepalivetime = 1000 * 60;
		aliveIn.keepaliveinterval = 1000 * 10;
		unsigned long ul = 0;
		if (SOCKET_ERROR == WSAIoctl(ic->s, SIO_KEEPALIVE_VALS, &aliveIn, sizeof(aliveIn), &aliveOut, sizeof(aliveOut), &ul, NULL, NULL))
			return false;
		CreateIoCompletionPort((HANDLE)ic->s, completionPort, (ULONG_PTR)sc, 0);
		//call WSARecv
		DWORD dw = 0, dw2 = 0;
		WSARecv(ic->s, &ic->wsaBuf, 1, &dw, &dw2, &ic->overLapped, NULL);
		return true;
	}

	void postSend(SocketContext * sc, IoContext * ic) {

	}
};


//Thread constructer
Thread::Thread(LPVOID lpParam) {
	if (threadNum == 0) {
		InitializeCriticalSection(&Thread::lock);
	}

	EnterCriticalSection(&lock);

	currentNum = ++threadNum;

	CreateThread(0, 0, run, (void *)lpParam, 0, 0);

	LeaveCriticalSection(&lock);
}

DWORD WINAPI Thread::run(LPVOID lpParam) {

	Iocp * iocp = (Iocp *)lpParam;

	DWORD dw = 0;
	OVERLAPPED * ol = NULL;
	SocketContext * socketContext = NULL, *newSocketContext = NULL;
	IoContext * acceptIoContext = NULL, *recvIoContext = NULL;

	while (true) {
		GetQueuedCompletionStatus(iocp->completionPort, &dw, (PULONG_PTR)&socketContext, (LPOVERLAPPED *)&ol, INFINITE);

		switch (socketContext->type) {
		case SocketContext::ioType::accept:
			//create new io context for new post accept
			acceptIoContext = new IoContext();
			iocp->postAccept(socketContext, acceptIoContext);
			//create new SocketContext and move last acceptED io context here
			newSocketContext = new SocketContext();
			//mark new socket context as recv op
			newSocketContext->type = SocketContext::ioType::recv;
			//get acceptED io context
			recvIoContext = CONTAINING_RECORD(ol, IoContext, overLapped);
			newSocketContext->ioContext = recvIoContext;
			//post recv
			iocp->postRecv(newSocketContext, recvIoContext);
			break;
		case SocketContext::ioType::recv:
			//get recv io context
			recvIoContext = CONTAINING_RECORD(ol, IoContext, overLapped);
			EnterCriticalSection(&Thread::lock);
			std::cout << recvIoContext->wsaBuf.buf << endl;
			LeaveCriticalSection(&Thread::lock);
			//post next recv io context
			iocp->postRecv(socketContext, recvIoContext);
			break;
		case SocketContext::ioType::send:
			break;
		}
	}
	return 1;
}
//init static val of Thread class
int Thread::threadNum = 0;
CRITICAL_SECTION Thread::lock;

int main(){
	
	Iocp * i = new Iocp();

	i->init();
	i->start();

	system("pause");

	return 0;

};

 
