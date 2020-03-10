#include "stdafx.h"
#include "server.h"

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

void Http::start() {

}

long Http::calculateCallback(char name[], int len) {
	int r = 0;
	for (int i = 0; i < len; i++) {
		r += name[i];
	}
	return r;
}

void Http::insertCallback(char name[], int len, void_function cb) {
	pair<long, void_function> pair;
	pair.first = calculateCallback(name, len);
	pair.second = cb;
	http_cbs.insert(pair);
}

void Http::insertDataCallback(char name[], int len, void_function cb) {
	pair<long, void_function> pair;
	pair.first = calculateCallback(name, len);
	pair.second = cb;
	http_data_cbs.insert(pair);
}

unsigned long len(char * a) {
	unsigned long i;
	for (i = 0; a[i] != '\0'; i++);
	return i;
}

void Http::postRecv(SocketHttpContext * shc, HttpContext * hc) {

}

void Http::postSend(SocketHttpContext * shc, HttpContext * hc) {

}

void Http::dealRecvHelloWorld(SocketHttpContext * shc) {
	char response[] =
		"HTTP/1.1 200 OK\r\n\
		Date: Tue, 10 Mar 2020 14 : 32 : 59 GMT\r\n\
		Server : Apache / 2.2.14 (Win32)\r\n\
		Last - Modified : Wed, 22 Jul 2009 19 : 15 : 56 GMT\r\n\
		Content - Length : 88\r\n\
		Content - Type : text / html\r\n\
		Connection : Closed\r\n\r\n\
		<html>\
		<body>\
		<h1>Hello, World!</h1>\
		</body>\
		</html>";
	unsigned long length = len(response);
	shc->socketContext->ioContext->wsaBuf.len = length;
	shc->socketContext->ioContext->wsaBuf.buf;
	//copy response data to shc's buf
	memcpy_s(shc->socketContext->ioContext->wsaBuf.buf, length, response, length);
	DWORD dw = length;
	PostQueuedCompletionStatus(completionPort, dw, (ULONG_PTR)shc, &shc->socketContext->ioContext->overLapped);
}

void Http::dealRecv(SocketHttpContext * shc) {
}

//---------------------Thread for http---------------

DWORD WINAPI ThreadForHttp::run(LPVOID lpParam) {

}