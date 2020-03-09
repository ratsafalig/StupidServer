#include "stdafx.h"
#include "server.h"
#include "http_parser.h"

int my_url_callback(http_parser* parser, const char *at, size_t length) {
	
	return 0;
}

int my_header_field_callback(http_parser * parser, const char * at, size_t length) {
	return 0;
}

int my_header_value_callback(http_parser * parser, const char * at, size_t length) {
	return 0;
}

int my_body_callback(http_parser * parser, const char * at, size_t length) {
	return 0;
}

int my_chunk_header_callback(http_parser * parser) {
	return 0;
}

int my_chunk_complete_callback(http_parser * parser) {
	return 0;
}

int my_message_begin_callback(http_parser * parser) {
	return 0;
}


void http_parsed_test(const char * buf, size_t len) {
	http_parser_settings settings;
	http_parser * parser = (http_parser *)malloc(sizeof(http_parser));
	ZeroMemory(&settings, sizeof(http_parser_settings));
	ZeroMemory(parser, sizeof(http_parser));
	settings.on_url = my_url_callback;
	settings.on_header_field = my_header_field_callback;
	settings.on_header_value = my_header_value_callback;
	settings.on_body = my_body_callback;
	settings.on_chunk_header = my_chunk_header_callback;
	settings.on_chunk_complete = my_chunk_complete_callback;
	settings.on_message_begin = my_message_begin_callback;
	http_parser_init(parser, HTTP_REQUEST);
	int nparsed = http_parser_execute(parser, &settings, buf, len);
}

//-----------------------------------HTTP-----------------------------------------

Http::Http() {

}

void Http::init() {
	parser = (http_parser *)malloc(sizeof(http_parser));
	ZeroMemory(parser, sizeof(http_parser));
	ZeroMemory(&settings, sizeof(http_parser_settings));
	http_parser_init(parser, HTTP_REQUEST);
	completionPort = CreateIoCompletionPort((HANDLE)INVALID_SOCKET, NULL, NULL, 0);
}

long Http::calculateCallback(char name[], int len) {
	int r = 0;
	for (int i = 0; i < len; i++) {
		r += name[i];
	}
	return r;
}

void Http::insertCallback(char name[], int len, void_function_http cb) {
	pair<long, void_function_http> pair;
	pair.first = calculateCallback(name, len);
	pair.second = cb;
	http_cbs.insert(pair);
}

void Http::insertDataCallback(char name[], int len, void_function_http cb) {
	pair<long, void_function_http> pair;
	pair.first = calculateCallback(name, len);
	pair.second = cb;
	http_data_cbs.insert(pair);
}

/*
template<char[] name, int len, Http * http, SocketHttpContext * shc>
int callback(http_parser *, const char * at, int len) {
	for (int i = 0; i < len; i++) {
		if (name[i] != at[i])
			return 0;
	}
	http->dealRecv(shc);
	return 1;
}
*/
int len(char * a) {
	int i;
	for (i = 0; a[i] != '\0'; i++);
	return i;
}
void Http::dealRecv(SocketHttpContext * shc) {
	//-------test----------
	char c[] = 
		"HTTP/1.1 200 OK\r\nDate: Mon, 09 Mar 2020 15:30 : 14 GMT\r\nContent - Type : application / json; charset = utf - 8\r\nContent - Length: 719\r\nConnection : keep - alive\r\nAccess - Control - Allow - Credentials : true\r\nAccess - Control - Allow - Methods : GET, POST\r\nAccess - Control - Allow - Origin : https ://space.bilibili.com\r\nBili - Trace - Id : 7578c7b58a5e6661\r\nVary : Origin\r\nAccess - Control - Allow - Headers : Origin, No - Cache, X - Requested - With, If - Modified - Since, Pragma, Last - Modified, Cache - Control, Expires, Content - Type, Access - Control - Allow - Credentials, DNT, X - CustomHeader, Keep - Alive, User - Agent, X - Cache - Webcdn\r\nAccess - Control - Expose - Headers : X - Cache - Webcdn\r\nExpires : Mon, 09 Mar 2020 15:30 : 13 GMT\r\nCache - Control : no - cache\r\nX - Cache - Webcdn : BYPASS from hw - sh3 - webcdn - 11\r\n\r\nhello world";
	int l = len(c);
	shc->socketContext->ioContext->wsaBuf = {
		(ULONG)l,
		c
	};
	DWORD dw = l;
	PostQueuedCompletionStatus(completionPort, dw, (ULONG_PTR)shc, &shc->socketContext->ioContext->overLapped);
	//-------/test---------
}

//-----------------------------------IOCP------------------------------------------

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
	EnterCriticalSection(&ThreadForIocp::lock);
	std::cout << "~IoContext()" << std::endl;
	LeaveCriticalSection(&ThreadForIocp::lock);

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
	EnterCriticalSection(&ThreadForIocp::lock);
	std::cout << "deleting a io socket context" << std::endl;
	LeaveCriticalSection(&ThreadForIocp::lock);
	//release heap space
	delete ioContext;
	delete this;
}

Iocp::Iocp() {
}

Iocp::~Iocp() {
}

//0.post accept
//1.run thread
void Iocp::start(int i) {
	DWORD dw = 0;
	IoContext * acceptIoContext = NULL;

	for (int j = 0; j < i; j++) {
		acceptIoContext = new IoContext();
		postAccept(listenSocketContext, acceptIoContext);
	}

	for (int j = 0; j < i; j++) {
		ThreadForIocp * t = new ThreadForIocp(this);
	}
}

//1.init wsa service
//2.create iocp
//3.bind and listen
bool Iocp::init() {
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

bool Iocp::postAccept(SocketContext * sc, IoContext * ic) {
	if (sc == NULL || ic == NULL)
		return false;
	int error;
	error = fnAcceptEx(sc->ioContext->s, ic->s, ic->wsaBuf.buf, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &ic->numberOfBytes, &ic->overLapped);
	error = GetLastError();
	return true;
}

bool Iocp::dealAccept(SocketContext * sc, IoContext * ic) {
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

bool Iocp::postRecv(SocketContext * sc, IoContext * ic) {
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
	CreateIoCompletionPort((HANDLE)ic->s, completionPort, (ULONG_PTR)sc, NULL);
	//call WSARecv
	error = WSARecv(ic->s, &ic->wsaBuf, 1, &ic->numberOfBytes, &ic->flags, &ic->overLapped, NULL);
	error = GetLastError();
	return true;
}

bool Iocp::dealRecv(SocketContext * sc, IoContext * ic) {
	DWORD dw = 0;
	OVERLAPPED * ol = NULL;
	SocketHttpContext * socketHttpContext = new SocketHttpContext();
	sc->type = SocketContext::ioType::send;
	socketHttpContext->socketContext = sc;
	socketHttpContext->socketContext->ioContext->resetOverLapped();
	//upper call
	http->dealRecv(socketHttpContext);
	//-------------------------------
	GetQueuedCompletionStatus(http->completionPort, &dw, (PULONG_PTR)&socketHttpContext, &ol, INFINITE);
	socketHttpContext->socketContext->type = SocketContext::ioType::send;
	postSend(socketHttpContext->socketContext, ic);
	//postRecv(socketHttpContext->socketContext, ic);
	return true;
}

bool Iocp::postSend(SocketContext * sc, IoContext * ic) {
	if (sc == NULL || ic == NULL)
		return false;
	ic->resetOverLapped();
	WSASend(sc->ioContext->s, &ic->wsaBuf, 1, &ic->numberOfBytes, 0, &ic->overLapped, NULL);
	return true;
}

//Thread constructer
ThreadForIocp::ThreadForIocp(LPVOID lpParam) {
	if (threadNum == 0) {
		InitializeCriticalSection(&ThreadForIocp::lock);
	}

	EnterCriticalSection(&lock);
	currentNum = ++threadNum;
	CreateThread(0, 0, run, (void *)lpParam, 0, 0);
	LeaveCriticalSection(&lock);
}

DWORD WINAPI ThreadForIocp::run(LPVOID lpParam) {

	Iocp * iocp = (Iocp *)lpParam;

	DWORD numberOfBytesTransferred = 0;
	OVERLAPPED * ol = NULL;
	SocketContext * socketContext = NULL;
	IoContext * ioContext = NULL;

	while (true) {
		BOOL r = GetQueuedCompletionStatus(iocp->completionPort, &numberOfBytesTransferred, (PULONG_PTR)&socketContext, (LPOVERLAPPED *)&ol, INFINITE);
		ioContext = CONTAINING_RECORD(ol, IoContext, overLapped);
		int error = GetLastError();
		switch (socketContext->type) {
		case SocketContext::ioType::accept:
			iocp->dealAccept(socketContext, ioContext);
			break;
		case SocketContext::ioType::recv:
			//ignore zero data
			if (numberOfBytesTransferred != 0) {
				iocp->dealRecv(socketContext, ioContext);
			}
			else if (send(socketContext->ioContext->s, "", 0, 0) == -1)
				delete socketContext;
			break;
		case SocketContext::ioType::send:
			cout << "send" << endl;
			break;
		}
	}
	return 1;
}
//init static val of Thread class
int ThreadForIocp::threadNum = 0;
CRITICAL_SECTION ThreadForIocp::lock;


int main(){

	Iocp * iocp = new Iocp();
	Http * http = new Http();

	//binding http service to iocp
	iocp->http = http;

	iocp->init();
	http->init();

	iocp->start();

	system("pause");

	return 0;
};


 
