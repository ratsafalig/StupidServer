#include "stdafx.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "server.h"

//listen port
#define PORT 10240
//wsabuf.buf size
#define BUFSIZE (1024*4)
//IoContext's guid
int IoContextId = 0;

IoContext::IoContext() : mark(IoContextId++){
	//alloc buffer
	wsaBuf.buf = (char *)::HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BUFSIZE);
	wsaBuf.len = BUFSIZE;
	//init overlapped
	ZeroMemory(&overLapped, sizeof(overLapped));
	//init listen socket
	s = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	numberOfBytes = 0;
	flags = 0;
}

IoContext::~IoContext() {
	EnterCriticalSection(&ThreadForTcp::lock);
	std::cout << "~IoContext()" << std::endl;
	LeaveCriticalSection(&ThreadForTcp::lock);

	//release socket and wsabuf
	closesocket(s);
	HeapFree(GetProcessHeap(), 0, wsaBuf.buf);
	delete this;
}

void IoContext::resetWsaBuf(){
	if(wsaBuf.buf != NULL)
		//keep in mind there is NOT &wasBuf.buf
		ZeroMemory(wsaBuf.buf, BUFSIZE);
	else
		wsaBuf.buf = (char *)::HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BUFSIZE);
}

void IoContext::resetOverLapped() {
	ZeroMemory(&overLapped, sizeof(overLapped));
}

SocketContext::SocketContext(){
	InitializeCriticalSection(&lock);

	EnterCriticalSection(&lock);

	ioContext = new IoContext();

	LeaveCriticalSection(&lock);
}

SocketContext::~SocketContext() {
	EnterCriticalSection(&ThreadForTcp::lock);
	std::cout << "~SocketContext()" << std::endl;
	LeaveCriticalSection(&ThreadForTcp::lock);
	//release heap space
	delete ioContext;
	delete this;
}

Tcp::Tcp() {
}

Tcp::~Tcp() {
}

//0.post accept
//1.run thread
void Tcp::start(int i) {
	DWORD dw = 0;
	IoContext * acceptIoContext = NULL;

	for (int j = 0; j < i; j++) {
		acceptIoContext = new IoContext();
		postAccept(listenSocketContext, acceptIoContext);
	}

	for (int j = 0; j < i; j++) {
		ThreadForTcp * t = new ThreadForTcp(this);
	}
}

//1.init wsa service
//2.create iocp
//3.bind and listen
bool Tcp::init() {
	//init wsa service
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	//create iocp
	tcpCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);

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
	if (NULL == CreateIoCompletionPort((HANDLE)listenSocketContext->ioContext->s, tcpCompletionPort, (ULONG_PTR)listenSocketContext, NULL))
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

bool Tcp::postAccept(SocketContext * sc, IoContext * ic) {
	if (sc == NULL || ic == NULL)
		return false;
	int error;
	error = fnAcceptEx(sc->ioContext->s, ic->s, ic->wsaBuf.buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &ic->numberOfBytes, &ic->overLapped);
	error = GetLastError();
	return true;
}

bool Tcp::dealAccept(SocketContext * sc, IoContext * ic) {
	//create new io context for new post accept
	IoContext * acceptIoContext = new IoContext();
	postAccept(sc, acceptIoContext);
	//create new SocketContext and move last acceptED io context here
	SocketContext * newSocketContext = new SocketContext();
	//mark new socket context as recv operation
	newSocketContext->type = SocketContext::ioType::recv;
	newSocketContext->ioContext = ic;
	//post recv
	postRecv(newSocketContext, ic);

	//get (remote)client info
	SOCKADDR_IN * localAddr = NULL, * remoteAddr = NULL;
	int localAddrLen = sizeof(SOCKADDR_IN), remoteAddrLen = sizeof(SOCKADDR_IN);
	fnGetAcceptExSockAddrs(ic->wsaBuf.buf, ic->numberOfBytes, localAddrLen, remoteAddrLen, (LPSOCKADDR *)&localAddr, &localAddrLen, (LPSOCKADDR *)&remoteAddr, &remoteAddrLen);
	memcpy_s(&ic->sockAddr, sizeof(SOCKADDR_IN), remoteAddr, sizeof(SOCKADDR_IN));

	return true;
}

bool Tcp::postRecv(SocketContext * sc, IoContext * ic) {
	if (sc == NULL || ic == NULL)
		return false;
	int error = 0;
	ic->resetOverLapped();
	ic->resetWsaBuf();

	//config IoContext
	tcp_keepalive aliveIn;
	tcp_keepalive aliveOut;
	aliveIn.onoff = TRUE;
	aliveIn.keepalivetime = 1000 * 60;
	aliveIn.keepaliveinterval = 1000 * 10;
	unsigned long ul = 0;
	error = WSAIoctl(ic->s, SIO_KEEPALIVE_VALS, &aliveIn, sizeof(aliveIn), &aliveOut, sizeof(aliveOut), &ul, NULL, NULL);
	if (SOCKET_ERROR == error)
		return false;
	CreateIoCompletionPort((HANDLE)ic->s, tcpCompletionPort, (ULONG_PTR)sc, NULL);
	//call WSARecv
	error = WSARecv(ic->s, &ic->wsaBuf, 1, &ic->numberOfBytes, &ic->flags, &ic->overLapped, NULL);
	error = GetLastError();
	return true;
}

bool Tcp::dealRecv(SocketContext * sc, IoContext * ic) {
	DWORD dw = 0;
	OVERLAPPED * ol = NULL;
	SocketHttpContext * socketHttpContext = new SocketHttpContext();
	sc->type = SocketContext::ioType::send;
	socketHttpContext->socketContext = sc;
	socketHttpContext->socketContext->ioContext->resetOverLapped();
	//upper call hello world
	//http->dealRecvHelloWorld(socketHttpContext);
	http->dealRecv(socketHttpContext);
	//-------------------------------
	GetQueuedCompletionStatus(http->exchangeCompletionPort, &dw, (PULONG_PTR)&socketHttpContext, &ol, INFINITE);
	socketHttpContext->socketContext->type = SocketContext::ioType::send;
	postSend(socketHttpContext->socketContext, ic);
	//postRecv(socketHttpContext->socketContext, ic);
	return true;
}

bool Tcp::postSend(SocketContext * sc, IoContext * ic) {
	if (sc == NULL || ic == NULL)
		return false;
	ic->resetOverLapped();
	WSASend(sc->ioContext->s, &ic->wsaBuf, 1, &ic->numberOfBytes, 0, &ic->overLapped, NULL);
	return true;
}

//Thread constructer
ThreadForTcp::ThreadForTcp(LPVOID lpParam) {
	if (threadNum == 0) {
		InitializeCriticalSection(&ThreadForTcp::lock);
	}

	EnterCriticalSection(&lock);
	currentNum = ++threadNum;
	CreateThread(0, 0, run, (void *)lpParam, 0, 0);
	LeaveCriticalSection(&lock);
}

DWORD WINAPI ThreadForTcp::run(LPVOID lpParam) {

	Tcp * tcp = (Tcp *)lpParam;

	DWORD numberOfBytesTransferred = 0;
	OVERLAPPED * ol = NULL;
	SocketContext * socketContext = NULL;
	IoContext * ioContext = NULL;

	while (true) {
		BOOL r = GetQueuedCompletionStatus(tcp->tcpCompletionPort, &numberOfBytesTransferred, (PULONG_PTR)&socketContext, (LPOVERLAPPED *)&ol, INFINITE);
		socketContext->ioContext->numberOfBytes = numberOfBytesTransferred;
		ioContext = CONTAINING_RECORD(ol, IoContext, overLapped);
		int error = GetLastError();
		switch (socketContext->type) {
		case SocketContext::ioType::accept:
			tcp->dealAccept(socketContext, ioContext);
			break;
		case SocketContext::ioType::recv:
			//ignore zero data
			if (numberOfBytesTransferred != 0) {
				tcp->dealRecv(socketContext, ioContext);
			}
			else if (send(socketContext->ioContext->s, "", 0, 0) == -1)
				delete socketContext;
			break;
		case SocketContext::ioType::send:
			cout << "case SocketContext::ioType::send" << endl;
			break;
		}
	}
	return 1;
}
//init static val of Thread class
int ThreadForTcp::threadNum = 0;
CRITICAL_SECTION ThreadForTcp::lock;

void normalStart() {
	Tcp * tcp = new Tcp();
	Http * http = new Http();

	//binding http service to iocp
	tcp->http = http;

	tcp->init();
	http->init();

	tcp->start();
	http->start();

	system("pause");
}

void testStart() {
	char buffer[] = "\
GET / HTTP/1.1\r\n\
Host : 127.0.0.1:9999\r\n\
Connection : keep-alive\r\n\
Cache-Control : max-age=0\r\n\
Upgrade-Insecure-Requests : 1\r\n\
User-Agent : Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/81.0.4044.43 Safari/537.36 Edg/81.0.416.28\r\n\
Accept : text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n\
Accept-Encoding : gzip, deflate, br\r\n\
Accept-Language : zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6\r\n\r\n";

	int length = sizeof(buffer);
	WSADATA data;
	BOOL r = 0;
	WSAStartup(MAKEWORD(2, 2), &data);
	SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client == INVALID_SOCKET)
		cout << "INVALID_SOCKET" << endl;
	r = GetLastError();
	struct sockaddr_in remote;
	remote.sin_family = AF_INET;
	remote.sin_port = htons(9999);
	remote.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	r = connect(client, (struct sockaddr *)&remote, sizeof(remote));
	r = GetLastError();
	r = send(client, buffer, length, 0);
	r = GetLastError();
	char recvBuffer[10000];
	ZeroMemory(recvBuffer, 10000);
	r = recv(client, recvBuffer, 10000, 0);
	r = GetLastError();
	cout << recvBuffer;
	WSACleanup();
}

int main(){

	Tcp * tcp = new Tcp();
	Http * http = new Http();

	//binding http service to iocp
	tcp->http = http;

	tcp->init();
	http->init();

	http->map((char *)"/a", 2, (char*)"hello_world.html", 17);

	tcp->start();
	http->start();

	system("pause");
	return 0;
};

