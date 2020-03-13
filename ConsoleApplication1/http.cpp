#include "stdafx.h"
#include "server.h"

//----------Utility----------

int chunkEncoding(char source[], int source_len,char dst[]) {
	size_t r0 = source_len;
	size_t r1 = sizeof(HTTP_RESPONSE_HEAD);
	size_t r2 = sizeof(HTTP_RESPONSE_TAIL);
	size_t dst_len = r0 + r1 + r2 - 2;
	char hex[10] = { '\0' };
	//r0 to hex
	size_t temp;
	size_t j = 0;
	for (size_t i = 28;i <= 28;i -= 4) {
		temp = r0 >> i;
		if (temp > 0) {
			hex[j++] = '0' + (temp <= 9 ? temp : (temp + 39));
			r0 -= (temp << i);
		}
	}
	memcpy_s(dst, sizeof(HTTP_RESPONSE_HEAD) - 1, HTTP_RESPONSE_HEAD, sizeof(HTTP_RESPONSE_HEAD) - 1);
	memcpy_s(dst + sizeof(HTTP_RESPONSE_HEAD) - 1, j, hex, j);
	memcpy_s(dst + sizeof(HTTP_RESPONSE_HEAD) - 1 + j, 2, "\r\n", 2);
	memcpy_s(dst + sizeof(HTTP_RESPONSE_HEAD) - 1 + j + 2, source_len, source, source_len);
	memcpy_s(dst + sizeof(HTTP_RESPONSE_HEAD) - 1 + j + 2 +source_len, sizeof(HTTP_RESPONSE_TAIL), HTTP_RESPONSE_TAIL, sizeof(HTTP_RESPONSE_TAIL));
	return dst_len + j;
}

size_t calculateHash(char c[], int len) {
	size_t r = 0;
	for (int i = 0;i < len;i++)
		r += c[i];
	return r;
}

//----------Context----------

HttpContext::HttpContext() {
	ZeroMemory(&overLapped, sizeof(OVERLAPPED));
	numberOfBytes = 0;
}

void HttpContext::resetOverLapped() {
	ZeroMemory(&overLapped, sizeof(OVERLAPPED));
}

SocketHttpContext::SocketHttpContext() {
	httpContext = new HttpContext();
	socketContext = new SocketContext();
}

SocketHttpContext::~SocketHttpContext() {

}

//----------Http----------

Http::Http() {

}

void Http::init() {
	parser = (http_parser *)malloc(sizeof(http_parser));
	ZeroMemory(parser, sizeof(http_parser));
	ZeroMemory(&settings, sizeof(http_parser_settings));
	parser->data = this;
	http_parser_init(parser, HTTP_REQUEST);
	//settings.on_url = ON_URL;

	settings.on_url = [](http_parser * parser, const char * at, size_t len)->int {
		Http::custom_data * data = (Http::custom_data*)parser->data;
		switch (data->type) {
		case URL_TYPE_MAP_ONLY:
			//get url's mapping file name
			char * file = NULL;
			size_t file_len = 0;
			pair<char*, int> p = data->http->mappings.at(calculateHash((char*)at, len));
			file = p.first;
			file_len = p.second;
			DWORD dw = 1000, read;
			OVERLAPPED ol;
			//without http header's raw data
			char raw_buffer[FILE_MAX_BUFFER_SIZE];
			//with http header packed data
			char response_buffer[HTTP_MAX_RESPONSE_SIZE];
			size_t response_buffer_length = 0;
			ZeroMemory(response_buffer, HTTP_MAX_RESPONSE_SIZE);
			WCHAR wfile[FILE_MAX_NAME_SIZE];
			ZeroMemory(wfile, sizeof(wfile));
			size_t numOfCharConverted = 0;
			//convert to wchar and create file handle for reading
			mbstowcs_s(&numOfCharConverted, wfile, file_len, file, FILE_MAX_NAME_SIZE);
			HANDLE fileHandle = CreateFile(wfile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
			//set shc type to processing
			data->shc->httpContext->type = HttpContext::ioType::processing;
			//reset httpContext's overlapped
			data->shc->httpContext->resetOverLapped();
			//bind to http's iocp and then start asyn read
			CreateIoCompletionPort(fileHandle, data->http->httpCompletionPort, (ULONG_PTR)data->shc, NULL);
			ReadFile(fileHandle, (LPVOID)raw_buffer, dw, NULL, &data->shc->httpContext->overLapped);
			//wait read complete
			GetQueuedCompletionStatus(data->http->httpCompletionPort, &read, (PULONG_PTR)&data->shc, (LPOVERLAPPED *)&ol, INFINITE);
			response_buffer_length = chunkEncoding(raw_buffer, read, response_buffer);
			cout << response_buffer;
			//set status done
			data->shc->httpContext->type = HttpContext::ioType::done;
			data->shc->httpContext->overLapped = ol;
			//copy response buffer to socketContext's buffer
			memcpy_s(data->shc->socketContext->ioContext->wsaBuf.buf, response_buffer_length, response_buffer, response_buffer_length);
			//pass into tcp server and ready to sent to client
			PostQueuedCompletionStatus(data->http->exchangeCompletionPort, response_buffer_length, (ULONG_PTR)data->shc, &data->shc->socketContext->ioContext->overLapped);
			break;
		}
		return 1;
	};

	exchangeCompletionPort = CreateIoCompletionPort((HANDLE)INVALID_SOCKET, NULL, NULL, 0);
	httpCompletionPort = CreateIoCompletionPort((HANDLE)INVALID_SOCKET, NULL, NULL, 0);
}

void Http::start() {

}

void Http::dealRecvHelloWorld(SocketHttpContext * shc) {
	char response[] =
"HTTP/1.1 200 OK\r\n\
Content-Type: text/plain; charset=utf-8\r\n\
Date: Thu, 12 Mar 2020 12:55:10 GMT\r\n\
Connection: keep - alive\r\n\
Transfer-Encoding: chunked\r\n\
\r\n\
b\r\n\
hello world\r\n\
0\r\n\
\r\n";
	shc->socketContext->ioContext->wsaBuf.len = sizeof(response);
	shc->socketContext->ioContext->wsaBuf.buf;
	//copy response data to shc's buf
	memcpy_s(shc->socketContext->ioContext->wsaBuf.buf, sizeof(response), response, sizeof(response));
	PostQueuedCompletionStatus(exchangeCompletionPort, sizeof(response) - 1, (ULONG_PTR)shc, &shc->socketContext->ioContext->overLapped);
}

void Http::dealRecv(SocketHttpContext * shc) {
	custom_data * data = new custom_data();
	data->http = this;
	data->type = 0;
	data->shc = shc;
	//bind data to parser
	parser->data = data;
	http_parser_execute(parser, &settings, shc->socketContext->ioContext->wsaBuf.buf, shc->socketContext->ioContext->numberOfBytes);
}

void Http::dealSend(SocketHttpContext * shc) {
	
}

void Http::map(char * url,size_t url_len, char * file, size_t file_len) {
	mappings.insert(pair<size_t, pair<char*, int>>(calculateHash((char *)url, url_len), pair<char*, size_t>((char *)file, file_len)));
}

//----------Thread for http----------

DWORD WINAPI ThreadForHttp::run(LPVOID lpParam) {
	return 0;
}