#include "stdafx.h"
#include "http_parser.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h> /* rand */
#include <string.h>
#include <stdarg.h>

#if defined(__APPLE__)
# undef strlncpy
#endif  /* defined(__APPLE__) */

#undef TRUE
#define TRUE 1
#undef FALSE
#define FALSE 0

#define MAX_HEADERS 13
#define MAX_ELEMENT_SIZE 2048
#define MAX_CHUNKS 16

#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))


static http_parser parser;

struct message {
	const char *name; // for debugging purposes
	const char *raw;
	enum http_parser_type type;
	enum http_method method;
	int status_code;
	char response_status[MAX_ELEMENT_SIZE];
	char request_path[MAX_ELEMENT_SIZE];
	char request_url[MAX_ELEMENT_SIZE];
	char fragment[MAX_ELEMENT_SIZE];
	char query_string[MAX_ELEMENT_SIZE];
	char body[MAX_ELEMENT_SIZE];
	size_t body_size;
	const char *host;
	const char *userinfo;
	uint16_t port;
	int num_headers;
	enum { NONE = 0, FIELD, VALUE } last_header_element;
	char headers[MAX_HEADERS][2][MAX_ELEMENT_SIZE];
	int should_keep_alive;

	int num_chunks;
	int num_chunks_complete;
	int chunk_lengths[MAX_CHUNKS];

	const char *upgrade; // upgraded body

	unsigned short http_major;
	unsigned short http_minor;

	int message_begin_cb_called;
	int headers_complete_cb_called;
	int message_complete_cb_called;
	int status_cb_called;
	int message_complete_on_eof;
	int body_is_final;
};

static int currently_parsing_eof;

static struct message messages[5];
static int num_messages;
static http_parser_settings *current_pause_parser;


/* * R E Q U E S T S * */
const struct message requests[] =
#define CURL_GET 0
{ { .name = "curl get"
,.type = HTTP_REQUEST
,.raw = "GET /test HTTP/1.1\r\n"
"User-Agent: curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1\r\n"
"Host: 0.0.0.0=5000\r\n"
"Accept: */*\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/test"
,.request_url = "/test"
,.num_headers = 3
,.headers =
{ { "User-Agent", "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" }
,{ "Host", "0.0.0.0=5000" }
,{ "Accept", "*/*" }
}
,.body = ""
	}

#define FIREFOX_GET 1
	,{ .name = "firefox get"
	,.type = HTTP_REQUEST
	,.raw = "GET /favicon.ico HTTP/1.1\r\n"
	"Host: 0.0.0.0=5000\r\n"
	"User-Agent: Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0\r\n"
	"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
	"Accept-Language: en-us,en;q=0.5\r\n"
	"Accept-Encoding: gzip,deflate\r\n"
	"Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"
	"Keep-Alive: 300\r\n"
	"Connection: keep-alive\r\n"
	"\r\n"
	,.should_keep_alive = TRUE
	,.message_complete_on_eof = FALSE
	,.http_major = 1
	,.http_minor = 1
	,.method = HTTP_GET
	,.query_string = ""
	,.fragment = ""
	,.request_path = "/favicon.ico"
	,.request_url = "/favicon.ico"
	,.num_headers = 8
	,.headers =
{ { "Host", "0.0.0.0=5000" }
,{ "User-Agent", "Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9) Gecko/2008061015 Firefox/3.0" }
,{ "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" }
,{ "Accept-Language", "en-us,en;q=0.5" }
,{ "Accept-Encoding", "gzip,deflate" }
,{ "Accept-Charset", "ISO-8859-1,utf-8;q=0.7,*;q=0.7" }
,{ "Keep-Alive", "300" }
,{ "Connection", "keep-alive" }
}
,.body = ""
}

#define DUMBLUCK 2
,{ .name = "dumbluck"
,.type = HTTP_REQUEST
,.raw = "GET /dumbluck HTTP/1.1\r\n"
"aaaaaaaaaaaaa:++++++++++\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/dumbluck"
,.request_url = "/dumbluck"
,.num_headers = 1
,.headers =
{ { "aaaaaaaaaaaaa",  "++++++++++" }
}
,.body = ""
}

#define FRAGMENT_IN_URI 3
,{ .name = "fragment in url"
,.type = HTTP_REQUEST
,.raw = "GET /forums/1/topics/2375?page=1#posts-17408 HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = "page=1"
,.fragment = "posts-17408"
,.request_path = "/forums/1/topics/2375"
/* XXX request url does include fragment? */
,.request_url = "/forums/1/topics/2375?page=1#posts-17408"
,.num_headers = 0
,.body = ""
}

#define GET_NO_HEADERS_NO_BODY 4
,{ .name = "get no headers no body"
,.type = HTTP_REQUEST
,.raw = "GET /get_no_headers_no_body/world HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE /* would need Connection: close */
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/get_no_headers_no_body/world"
,.request_url = "/get_no_headers_no_body/world"
,.num_headers = 0
,.body = ""
}

#define GET_ONE_HEADER_NO_BODY 5
,{ .name = "get one header no body"
,.type = HTTP_REQUEST
,.raw = "GET /get_one_header_no_body HTTP/1.1\r\n"
"Accept: */*\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE /* would need Connection: close */
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/get_one_header_no_body"
,.request_url = "/get_one_header_no_body"
,.num_headers = 1
,.headers =
{ { "Accept" , "*/*" }
}
,.body = ""
}

#define GET_FUNKY_CONTENT_LENGTH 6
,{ .name = "get funky content length body hello"
,.type = HTTP_REQUEST
,.raw = "GET /get_funky_content_length_body_hello HTTP/1.0\r\n"
"conTENT-Length: 5\r\n"
"\r\n"
"HELLO"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/get_funky_content_length_body_hello"
,.request_url = "/get_funky_content_length_body_hello"
,.num_headers = 1
,.headers =
{ { "conTENT-Length" , "5" }
}
,.body = "HELLO"
}

#define POST_IDENTITY_BODY_WORLD 7
,{ .name = "post identity body world"
,.type = HTTP_REQUEST
,.raw = "POST /post_identity_body_world?q=search#hey HTTP/1.1\r\n"
"Accept: */*\r\n"
"Content-Length: 5\r\n"
"\r\n"
"World"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = "q=search"
,.fragment = "hey"
,.request_path = "/post_identity_body_world"
,.request_url = "/post_identity_body_world?q=search#hey"
,.num_headers = 2
,.headers =
{ { "Accept", "*/*" }
,{ "Content-Length", "5" }
}
,.body = "World"
}

#define POST_CHUNKED_ALL_YOUR_BASE 8
,{ .name = "post - chunked body: all your base are belong to us"
,.type = HTTP_REQUEST
,.raw = "POST /post_chunked_all_your_base HTTP/1.1\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"1e\r\nall your base are belong to us\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/post_chunked_all_your_base"
,.request_url = "/post_chunked_all_your_base"
,.num_headers = 1
,.headers =
{ { "Transfer-Encoding" , "chunked" }
}
,.body = "all your base are belong to us"
,.num_chunks_complete = 2
,.chunk_lengths = { 0x1e }
}

#define TWO_CHUNKS_MULT_ZERO_END 9
,{ .name = "two chunks ; triple zero ending"
,.type = HTTP_REQUEST
,.raw = "POST /two_chunks_mult_zero_end HTTP/1.1\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"5\r\nhello\r\n"
"6\r\n world\r\n"
"000\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/two_chunks_mult_zero_end"
,.request_url = "/two_chunks_mult_zero_end"
,.num_headers = 1
,.headers =
{ { "Transfer-Encoding", "chunked" }
}
,.body = "hello world"
,.num_chunks_complete = 3
,.chunk_lengths = { 5, 6 }
}

#define CHUNKED_W_TRAILING_HEADERS 10
,{ .name = "chunked with trailing headers. blech."
,.type = HTTP_REQUEST
,.raw = "POST /chunked_w_trailing_headers HTTP/1.1\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"5\r\nhello\r\n"
"6\r\n world\r\n"
"0\r\n"
"Vary: *\r\n"
"Content-Type: text/plain\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/chunked_w_trailing_headers"
,.request_url = "/chunked_w_trailing_headers"
,.num_headers = 3
,.headers =
{ { "Transfer-Encoding",  "chunked" }
,{ "Vary", "*" }
,{ "Content-Type", "text/plain" }
}
,.body = "hello world"
,.num_chunks_complete = 3
,.chunk_lengths = { 5, 6 }
}

#define CHUNKED_W_NONSENSE_AFTER_LENGTH 11
,{ .name = "with nonsense after the length"
,.type = HTTP_REQUEST
,.raw = "POST /chunked_w_nonsense_after_length HTTP/1.1\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"5; ilovew3;whattheluck=aretheseparametersfor\r\nhello\r\n"
"6; blahblah; blah\r\n world\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/chunked_w_nonsense_after_length"
,.request_url = "/chunked_w_nonsense_after_length"
,.num_headers = 1
,.headers =
{ { "Transfer-Encoding", "chunked" }
}
,.body = "hello world"
,.num_chunks_complete = 3
,.chunk_lengths = { 5, 6 }
}

#define WITH_QUOTES 12
,{ .name = "with quotes"
,.type = HTTP_REQUEST
,.raw = "GET /with_\"stupid\"_quotes?foo=\"bar\" HTTP/1.1\r\n\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = "foo=\"bar\""
,.fragment = ""
,.request_path = "/with_\"stupid\"_quotes"
,.request_url = "/with_\"stupid\"_quotes?foo=\"bar\""
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define APACHEBENCH_GET 13
/* The server receiving this request SHOULD NOT wait for EOF
* to know that content-length == 0.
* How to represent this in a unit test? message_complete_on_eof
* Compare with NO_CONTENT_LENGTH_RESPONSE.
*/
,{ .name = "apachebench get"
,.type = HTTP_REQUEST
,.raw = "GET /test HTTP/1.0\r\n"
"Host: 0.0.0.0:5000\r\n"
"User-Agent: ApacheBench/2.3\r\n"
"Accept: */*\r\n\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/test"
,.request_url = "/test"
,.num_headers = 3
,.headers = { { "Host", "0.0.0.0:5000" }
,{ "User-Agent", "ApacheBench/2.3" }
,{ "Accept", "*/*" }
}
,.body = ""
}

#define QUERY_URL_WITH_QUESTION_MARK_GET 14
/* Some clients include '?' characters in query strings.
*/
,{ .name = "query url with question mark"
,.type = HTTP_REQUEST
,.raw = "GET /test.cgi?foo=bar?baz HTTP/1.1\r\n\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = "foo=bar?baz"
,.fragment = ""
,.request_path = "/test.cgi"
,.request_url = "/test.cgi?foo=bar?baz"
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define PREFIX_NEWLINE_GET 15
/* Some clients, especially after a POST in a keep-alive connection,
* will send an extra CRLF before the next request
*/
,{ .name = "newline prefix get"
,.type = HTTP_REQUEST
,.raw = "\r\nGET /test HTTP/1.1\r\n\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/test"
,.request_url = "/test"
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define UPGRADE_REQUEST 16
,{ .name = "upgrade request"
,.type = HTTP_REQUEST
,.raw = "GET /demo HTTP/1.1\r\n"
"Host: example.com\r\n"
"Connection: Upgrade\r\n"
"Sec-WebSocket-Key2: 12998 5 Y3 1  .P00\r\n"
"Sec-WebSocket-Protocol: sample\r\n"
"Upgrade: WebSocket\r\n"
"Sec-WebSocket-Key1: 4 @1  46546xW%0l 1 5\r\n"
"Origin: http://example.com\r\n"
"\r\n"
"Hot diggity dogg"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/demo"
,.request_url = "/demo"
,.num_headers = 7
,.upgrade = "Hot diggity dogg"
,.headers = { { "Host", "example.com" }
,{ "Connection", "Upgrade" }
,{ "Sec-WebSocket-Key2", "12998 5 Y3 1  .P00" }
,{ "Sec-WebSocket-Protocol", "sample" }
,{ "Upgrade", "WebSocket" }
,{ "Sec-WebSocket-Key1", "4 @1  46546xW%0l 1 5" }
,{ "Origin", "http://example.com" }
}
,.body = ""
}

#define CONNECT_REQUEST 17
,{ .name = "connect request"
,.type = HTTP_REQUEST
,.raw = "CONNECT 0-home0.netscape.com:443 HTTP/1.0\r\n"
"User-agent: Mozilla/1.1N\r\n"
"Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
"\r\n"
"some data\r\n"
"and yet even more data"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_CONNECT
,.query_string = ""
,.fragment = ""
,.request_path = ""
,.request_url = "0-home0.netscape.com:443"
,.num_headers = 2
,.upgrade = "some data\r\nand yet even more data"
,.headers = { { "User-agent", "Mozilla/1.1N" }
,{ "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
}
,.body = ""
}

#define REPORT_REQ 18
,{ .name = "report request"
,.type = HTTP_REQUEST
,.raw = "REPORT /test HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_REPORT
,.query_string = ""
,.fragment = ""
,.request_path = "/test"
,.request_url = "/test"
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define NO_HTTP_VERSION 19
,{ .name = "request with no http version"
,.type = HTTP_REQUEST
,.raw = "GET /\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 0
,.http_minor = 9
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define MSEARCH_REQ 20
,{ .name = "m-search request"
,.type = HTTP_REQUEST
,.raw = "M-SEARCH * HTTP/1.1\r\n"
"HOST: 239.255.255.250:1900\r\n"
"MAN: \"ssdp:discover\"\r\n"
"ST: \"ssdp:all\"\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_MSEARCH
,.query_string = ""
,.fragment = ""
,.request_path = "*"
,.request_url = "*"
,.num_headers = 3
,.headers = { { "HOST", "239.255.255.250:1900" }
,{ "MAN", "\"ssdp:discover\"" }
,{ "ST", "\"ssdp:all\"" }
}
,.body = ""
}

#define LINE_FOLDING_IN_HEADER 21
,{ .name = "line folding in header value"
,.type = HTTP_REQUEST
,.raw = "GET / HTTP/1.1\r\n"
"Line1:   abc\r\n"
"\tdef\r\n"
" ghi\r\n"
"\t\tjkl\r\n"
"  mno \r\n"
"\t \tqrs\r\n"
"Line2: \t line2\t\r\n"
"Line3:\r\n"
" line3\r\n"
"Line4: \r\n"
" \r\n"
"Connection:\r\n"
" close\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 5
,.headers = { { "Line1", "abc\tdef ghi\t\tjkl  mno \t \tqrs" }
,{ "Line2", "line2\t" }
,{ "Line3", "line3" }
,{ "Line4", "" }
,{ "Connection", "close" },
}
,.body = ""
}


#define QUERY_TERMINATED_HOST 22
,{ .name = "host terminated by a query string"
,.type = HTTP_REQUEST
,.raw = "GET http://hypnotoad.org?hail=all HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = "hail=all"
,.fragment = ""
,.request_path = ""
,.request_url = "http://hypnotoad.org?hail=all"
,.host = "hypnotoad.org"
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define QUERY_TERMINATED_HOSTPORT 23
,{ .name = "host:port terminated by a query string"
,.type = HTTP_REQUEST
,.raw = "GET http://hypnotoad.org:1234?hail=all HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = "hail=all"
,.fragment = ""
,.request_path = ""
,.request_url = "http://hypnotoad.org:1234?hail=all"
,.host = "hypnotoad.org"
,.port = 1234
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define SPACE_TERMINATED_HOSTPORT 24
,{ .name = "host:port terminated by a space"
,.type = HTTP_REQUEST
,.raw = "GET http://hypnotoad.org:1234 HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = ""
,.request_url = "http://hypnotoad.org:1234"
,.host = "hypnotoad.org"
,.port = 1234
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define PATCH_REQ 25
,{ .name = "PATCH request"
,.type = HTTP_REQUEST
,.raw = "PATCH /file.txt HTTP/1.1\r\n"
"Host: www.example.com\r\n"
"Content-Type: application/example\r\n"
"If-Match: \"e0023aa4e\"\r\n"
"Content-Length: 10\r\n"
"\r\n"
"cccccccccc"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_PATCH
,.query_string = ""
,.fragment = ""
,.request_path = "/file.txt"
,.request_url = "/file.txt"
,.num_headers = 4
,.headers = { { "Host", "www.example.com" }
,{ "Content-Type", "application/example" }
,{ "If-Match", "\"e0023aa4e\"" }
,{ "Content-Length", "10" }
}
,.body = "cccccccccc"
}

#define CONNECT_CAPS_REQUEST 26
,{ .name = "connect caps request"
,.type = HTTP_REQUEST
,.raw = "CONNECT HOME0.NETSCAPE.COM:443 HTTP/1.0\r\n"
"User-agent: Mozilla/1.1N\r\n"
"Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_CONNECT
,.query_string = ""
,.fragment = ""
,.request_path = ""
,.request_url = "HOME0.NETSCAPE.COM:443"
,.num_headers = 2
,.upgrade = ""
,.headers = { { "User-agent", "Mozilla/1.1N" }
,{ "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
}
,.body = ""
}

#if !HTTP_PARSER_STRICT
#define UTF8_PATH_REQ 27
,{ .name = "utf-8 path request"
,.type = HTTP_REQUEST
,.raw = "GET /δ¶/δt/pope?q=1#narf HTTP/1.1\r\n"
"Host: github.com\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = "q=1"
,.fragment = "narf"
,.request_path = "/δ¶/δt/pope"
,.request_url = "/δ¶/δt/pope?q=1#narf"
,.num_headers = 1
,.headers = { { "Host", "github.com" }
}
,.body = ""
}

#define HOSTNAME_UNDERSCORE 28
,{ .name = "hostname underscore"
,.type = HTTP_REQUEST
,.raw = "CONNECT home_0.netscape.com:443 HTTP/1.0\r\n"
"User-agent: Mozilla/1.1N\r\n"
"Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_CONNECT
,.query_string = ""
,.fragment = ""
,.request_path = ""
,.request_url = "home_0.netscape.com:443"
,.num_headers = 2
,.upgrade = ""
,.headers = { { "User-agent", "Mozilla/1.1N" }
,{ "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
}
,.body = ""
}
#endif  /* !HTTP_PARSER_STRICT */

/* see https://github.com/ry/http-parser/issues/47 */
#define EAT_TRAILING_CRLF_NO_CONNECTION_CLOSE 29
,{ .name = "eat CRLF between requests, no \"Connection: close\" header"
,.raw = "POST / HTTP/1.1\r\n"
"Host: www.example.com\r\n"
"Content-Type: application/x-www-form-urlencoded\r\n"
"Content-Length: 4\r\n"
"\r\n"
"q=42\r\n" /* note the trailing CRLF */
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 3
,.upgrade = 0
,.headers = { { "Host", "www.example.com" }
,{ "Content-Type", "application/x-www-form-urlencoded" }
,{ "Content-Length", "4" }
}
,.body = "q=42"
}

/* see https://github.com/ry/http-parser/issues/47 */
#define EAT_TRAILING_CRLF_WITH_CONNECTION_CLOSE 30
,{ .name = "eat CRLF between requests even if \"Connection: close\" is set"
,.raw = "POST / HTTP/1.1\r\n"
"Host: www.example.com\r\n"
"Content-Type: application/x-www-form-urlencoded\r\n"
"Content-Length: 4\r\n"
"Connection: close\r\n"
"\r\n"
"q=42\r\n" /* note the trailing CRLF */
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE /* input buffer isn't empty when on_message_complete is called */
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 4
,.upgrade = 0
,.headers = { { "Host", "www.example.com" }
,{ "Content-Type", "application/x-www-form-urlencoded" }
,{ "Content-Length", "4" }
,{ "Connection", "close" }
}
,.body = "q=42"
}

#define PURGE_REQ 31
,{ .name = "PURGE request"
,.type = HTTP_REQUEST
,.raw = "PURGE /file.txt HTTP/1.1\r\n"
"Host: www.example.com\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_PURGE
,.query_string = ""
,.fragment = ""
,.request_path = "/file.txt"
,.request_url = "/file.txt"
,.num_headers = 1
,.headers = { { "Host", "www.example.com" } }
,.body = ""
}

#define SEARCH_REQ 32
,{ .name = "SEARCH request"
,.type = HTTP_REQUEST
,.raw = "SEARCH / HTTP/1.1\r\n"
"Host: www.example.com\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_SEARCH
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 1
,.headers = { { "Host", "www.example.com" } }
,.body = ""
}

#define PROXY_WITH_BASIC_AUTH 33
,{ .name = "host:port and basic_auth"
,.type = HTTP_REQUEST
,.raw = "GET http://a%12:b!&*$@hypnotoad.org:1234/toto HTTP/1.1\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.fragment = ""
,.request_path = "/toto"
,.request_url = "http://a%12:b!&*$@hypnotoad.org:1234/toto"
,.host = "hypnotoad.org"
,.userinfo = "a%12:b!&*$"
,.port = 1234
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define LINE_FOLDING_IN_HEADER_WITH_LF 34
,{ .name = "line folding in header value"
,.type = HTTP_REQUEST
,.raw = "GET / HTTP/1.1\n"
"Line1:   abc\n"
"\tdef\n"
" ghi\n"
"\t\tjkl\n"
"  mno \n"
"\t \tqrs\n"
"Line2: \t line2\t\n"
"Line3:\n"
" line3\n"
"Line4: \n"
" \n"
"Connection:\n"
" close\n"
"\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 5
,.headers = { { "Line1", "abc\tdef ghi\t\tjkl  mno \t \tqrs" }
,{ "Line2", "line2\t" }
,{ "Line3", "line3" }
,{ "Line4", "" }
,{ "Connection", "close" },
}
,.body = ""
}

#define CONNECTION_MULTI 35
,{ .name = "multiple connection header values with folding"
,.type = HTTP_REQUEST
,.raw = "GET /demo HTTP/1.1\r\n"
"Host: example.com\r\n"
"Connection: Something,\r\n"
" Upgrade, ,Keep-Alive\r\n"
"Sec-WebSocket-Key2: 12998 5 Y3 1  .P00\r\n"
"Sec-WebSocket-Protocol: sample\r\n"
"Upgrade: WebSocket\r\n"
"Sec-WebSocket-Key1: 4 @1  46546xW%0l 1 5\r\n"
"Origin: http://example.com\r\n"
"\r\n"
"Hot diggity dogg"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/demo"
,.request_url = "/demo"
,.num_headers = 7
,.upgrade = "Hot diggity dogg"
,.headers = { { "Host", "example.com" }
,{ "Connection", "Something, Upgrade, ,Keep-Alive" }
,{ "Sec-WebSocket-Key2", "12998 5 Y3 1  .P00" }
,{ "Sec-WebSocket-Protocol", "sample" }
,{ "Upgrade", "WebSocket" }
,{ "Sec-WebSocket-Key1", "4 @1  46546xW%0l 1 5" }
,{ "Origin", "http://example.com" }
}
,.body = ""
}

#define CONNECTION_MULTI_LWS 36
,{ .name = "multiple connection header values with folding and lws"
,.type = HTTP_REQUEST
,.raw = "GET /demo HTTP/1.1\r\n"
"Connection: keep-alive, upgrade\r\n"
"Upgrade: WebSocket\r\n"
"\r\n"
"Hot diggity dogg"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/demo"
,.request_url = "/demo"
,.num_headers = 2
,.upgrade = "Hot diggity dogg"
,.headers = { { "Connection", "keep-alive, upgrade" }
,{ "Upgrade", "WebSocket" }
}
,.body = ""
}

#define CONNECTION_MULTI_LWS_CRLF 37
,{ .name = "multiple connection header values with folding and lws"
,.type = HTTP_REQUEST
,.raw = "GET /demo HTTP/1.1\r\n"
"Connection: keep-alive, \r\n upgrade\r\n"
"Upgrade: WebSocket\r\n"
"\r\n"
"Hot diggity dogg"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_GET
,.query_string = ""
,.fragment = ""
,.request_path = "/demo"
,.request_url = "/demo"
,.num_headers = 2
,.upgrade = "Hot diggity dogg"
,.headers = { { "Connection", "keep-alive,  upgrade" }
,{ "Upgrade", "WebSocket" }
}
,.body = ""
}

#define UPGRADE_POST_REQUEST 38
,{ .name = "upgrade post request"
,.type = HTTP_REQUEST
,.raw = "POST /demo HTTP/1.1\r\n"
"Host: example.com\r\n"
"Connection: Upgrade\r\n"
"Upgrade: HTTP/2.0\r\n"
"Content-Length: 15\r\n"
"\r\n"
"sweet post body"
"Hot diggity dogg"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.request_path = "/demo"
,.request_url = "/demo"
,.num_headers = 4
,.upgrade = "Hot diggity dogg"
,.headers = { { "Host", "example.com" }
,{ "Connection", "Upgrade" }
,{ "Upgrade", "HTTP/2.0" }
,{ "Content-Length", "15" }
}
,.body = "sweet post body"
}

#define CONNECT_WITH_BODY_REQUEST 39
,{ .name = "connect with body request"
,.type = HTTP_REQUEST
,.raw = "CONNECT foo.bar.com:443 HTTP/1.0\r\n"
"User-agent: Mozilla/1.1N\r\n"
"Proxy-authorization: basic aGVsbG86d29ybGQ=\r\n"
"Content-Length: 10\r\n"
"\r\n"
"blarfcicle"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_CONNECT
,.request_url = "foo.bar.com:443"
,.num_headers = 3
,.upgrade = "blarfcicle"
,.headers = { { "User-agent", "Mozilla/1.1N" }
,{ "Proxy-authorization", "basic aGVsbG86d29ybGQ=" }
,{ "Content-Length", "10" }
}
,.body = ""
}

/* Examples from the Internet draft for LINK/UNLINK methods:
* https://tools.ietf.org/id/draft-snell-link-method-01.html#rfc.section.5
*/

#define LINK_REQUEST 40
,{ .name = "link request"
,.type = HTTP_REQUEST
,.raw = "LINK /images/my_dog.jpg HTTP/1.1\r\n"
"Host: example.com\r\n"
"Link: <http://example.com/profiles/joe>; rel=\"tag\"\r\n"
"Link: <http://example.com/profiles/sally>; rel=\"tag\"\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_LINK
,.request_path = "/images/my_dog.jpg"
,.request_url = "/images/my_dog.jpg"
,.query_string = ""
,.fragment = ""
,.num_headers = 3
,.headers = { { "Host", "example.com" }
,{ "Link", "<http://example.com/profiles/joe>; rel=\"tag\"" }
,{ "Link", "<http://example.com/profiles/sally>; rel=\"tag\"" }
}
,.body = ""
}

#define UNLINK_REQUEST 41
,{ .name = "unlink request"
,.type = HTTP_REQUEST
,.raw = "UNLINK /images/my_dog.jpg HTTP/1.1\r\n"
"Host: example.com\r\n"
"Link: <http://example.com/profiles/sally>; rel=\"tag\"\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_UNLINK
,.request_path = "/images/my_dog.jpg"
,.request_url = "/images/my_dog.jpg"
,.query_string = ""
,.fragment = ""
,.num_headers = 2
,.headers = { { "Host", "example.com" }
,{ "Link", "<http://example.com/profiles/sally>; rel=\"tag\"" }
}
,.body = ""
}

#define SOURCE_REQUEST 42
,{ .name = "source request"
,.type = HTTP_REQUEST
,.raw = "SOURCE /music/sweet/music HTTP/1.1\r\n"
"Host: example.com\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_SOURCE
,.request_path = "/music/sweet/music"
,.request_url = "/music/sweet/music"
,.query_string = ""
,.fragment = ""
,.num_headers = 1
,.headers = { { "Host", "example.com" } }
,.body = ""
}

#define SOURCE_ICE_REQUEST 42
,{ .name = "source request"
,.type = HTTP_REQUEST
,.raw = "SOURCE /music/sweet/music ICE/1.0\r\n"
"Host: example.com\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.method = HTTP_SOURCE
,.request_path = "/music/sweet/music"
,.request_url = "/music/sweet/music"
,.query_string = ""
,.fragment = ""
,.num_headers = 1
,.headers = { { "Host", "example.com" } }
,.body = ""
}

#define POST_MULTI_TE_LAST_CHUNKED 43
,{ .name = "post - multi coding transfer-encoding chunked body"
,.type = HTTP_REQUEST
,.raw = "POST / HTTP/1.1\r\n"
"Transfer-Encoding: deflate, chunked\r\n"
"\r\n"
"1e\r\nall your base are belong to us\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 1
,.headers =
{ { "Transfer-Encoding" , "deflate, chunked" }
}
,.body = "all your base are belong to us"
,.num_chunks_complete = 2
,.chunk_lengths = { 0x1e }
}

#define POST_MULTI_LINE_TE_LAST_CHUNKED 44
,{ .name = "post - multi line coding transfer-encoding chunked body"
,.type = HTTP_REQUEST
,.raw = "POST / HTTP/1.1\r\n"
"Transfer-Encoding: deflate,\r\n"
" chunked\r\n"
"\r\n"
"1e\r\nall your base are belong to us\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.method = HTTP_POST
,.query_string = ""
,.fragment = ""
,.request_path = "/"
,.request_url = "/"
,.num_headers = 1
,.headers =
{ { "Transfer-Encoding" , "deflate, chunked" }
}
,.body = "all your base are belong to us"
,.num_chunks_complete = 2
,.chunk_lengths = { 0x1e }
}
};

/* * R E S P O N S E S * */
const struct message responses[] =
#define GOOGLE_301 0
{ { .name = "google 301"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 301 Moved Permanently\r\n"
"Location: http://www.google.com/\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Date: Sun, 26 Apr 2009 11:11:49 GMT\r\n"
"Expires: Tue, 26 May 2009 11:11:49 GMT\r\n"
"X-$PrototypeBI-Version: 1.6.0.3\r\n" /* $ char in header field */
"Cache-Control: public, max-age=2592000\r\n"
"Server: gws\r\n"
"Content-Length:  219  \r\n"
"\r\n"
"<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
"<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
"<H1>301 Moved</H1>\n"
"The document has moved\n"
"<A HREF=\"http://www.google.com/\">here</A>.\r\n"
"</BODY></HTML>\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 301
,.response_status = "Moved Permanently"
,.num_headers = 8
,.headers =
{ { "Location", "http://www.google.com/" }
,{ "Content-Type", "text/html; charset=UTF-8" }
,{ "Date", "Sun, 26 Apr 2009 11:11:49 GMT" }
,{ "Expires", "Tue, 26 May 2009 11:11:49 GMT" }
,{ "X-$PrototypeBI-Version", "1.6.0.3" }
,{ "Cache-Control", "public, max-age=2592000" }
,{ "Server", "gws" }
,{ "Content-Length", "219  " }
}
,.body = "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
"<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
"<H1>301 Moved</H1>\n"
"The document has moved\n"
"<A HREF=\"http://www.google.com/\">here</A>.\r\n"
"</BODY></HTML>\r\n"
	}

#define NO_CONTENT_LENGTH_RESPONSE 1
	/* The client should wait for the server's EOF. That is, when content-length
	* is not specified, and "Connection: close", the end of body is specified
	* by the EOF.
	* Compare with APACHEBENCH_GET
	*/
	,{ .name = "no content-length response"
	,.type = HTTP_RESPONSE
	,.raw = "HTTP/1.1 200 OK\r\n"
	"Date: Tue, 04 Aug 2009 07:59:32 GMT\r\n"
	"Server: Apache\r\n"
	"X-Powered-By: Servlet/2.5 JSP/2.1\r\n"
	"Content-Type: text/xml; charset=utf-8\r\n"
	"Connection: close\r\n"
	"\r\n"
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
	"  <SOAP-ENV:Body>\n"
	"    <SOAP-ENV:Fault>\n"
	"       <faultcode>SOAP-ENV:Client</faultcode>\n"
	"       <faultstring>Client Error</faultstring>\n"
	"    </SOAP-ENV:Fault>\n"
	"  </SOAP-ENV:Body>\n"
	"</SOAP-ENV:Envelope>"
	,.should_keep_alive = FALSE
	,.message_complete_on_eof = TRUE
	,.http_major = 1
	,.http_minor = 1
	,.status_code = 200
	,.response_status = "OK"
	,.num_headers = 5
	,.headers =
{ { "Date", "Tue, 04 Aug 2009 07:59:32 GMT" }
,{ "Server", "Apache" }
,{ "X-Powered-By", "Servlet/2.5 JSP/2.1" }
,{ "Content-Type", "text/xml; charset=utf-8" }
,{ "Connection", "close" }
}
,.body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
"  <SOAP-ENV:Body>\n"
"    <SOAP-ENV:Fault>\n"
"       <faultcode>SOAP-ENV:Client</faultcode>\n"
"       <faultstring>Client Error</faultstring>\n"
"    </SOAP-ENV:Fault>\n"
"  </SOAP-ENV:Body>\n"
"</SOAP-ENV:Envelope>"
}

#define NO_HEADERS_NO_BODY_404 2
,{ .name = "404 no headers no body"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 404 Not Found\r\n\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 404
,.response_status = "Not Found"
,.num_headers = 0
,.headers = {}
,.body_size = 0
,.body = ""
}

#define NO_REASON_PHRASE 3
,{ .name = "301 no response phrase"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 301\r\n\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 301
,.response_status = ""
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define TRAILING_SPACE_ON_CHUNKED_BODY 4
,{ .name = "200 trailing space on chunked body"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/plain\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"25  \r\n"
"This is the data in the first chunk\r\n"
"\r\n"
"1C\r\n"
"and this is the second one\r\n"
"\r\n"
"0  \r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 2
,.headers =
{ { "Content-Type", "text/plain" }
,{ "Transfer-Encoding", "chunked" }
}
,.body_size = 37 + 28
,.body =
"This is the data in the first chunk\r\n"
"and this is the second one\r\n"
,.num_chunks_complete = 3
,.chunk_lengths = { 0x25, 0x1c }
}

#define NO_CARRIAGE_RET 5
,{ .name = "no carriage ret"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\n"
"Content-Type: text/html; charset=utf-8\n"
"Connection: close\n"
"\n"
"these headers are from http://news.ycombinator.com/"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 2
,.headers =
{ { "Content-Type", "text/html; charset=utf-8" }
,{ "Connection", "close" }
}
,.body = "these headers are from http://news.ycombinator.com/"
}

#define PROXY_CONNECTION 6
,{ .name = "proxy connection"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Content-Length: 11\r\n"
"Proxy-Connection: close\r\n"
"Date: Thu, 31 Dec 2009 20:55:48 +0000\r\n"
"\r\n"
"hello world"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 4
,.headers =
{ { "Content-Type", "text/html; charset=UTF-8" }
,{ "Content-Length", "11" }
,{ "Proxy-Connection", "close" }
,{ "Date", "Thu, 31 Dec 2009 20:55:48 +0000" }
}
,.body = "hello world"
}

#define UNDERSTORE_HEADER_KEY 7
// shown by
// curl -o /dev/null -v "http://ad.doubleclick.net/pfadx/DARTSHELLCONFIGXML;dcmt=text/xml;"
,{ .name = "underscore header key"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Server: DCLK-AdSvr\r\n"
"Content-Type: text/xml\r\n"
"Content-Length: 0\r\n"
"DCLK_imp: v7;x;114750856;0-0;0;17820020;0/0;21603567/21621457/1;;~okv=;dcmt=text/xml;;~cs=o\r\n\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 4
,.headers =
{ { "Server", "DCLK-AdSvr" }
,{ "Content-Type", "text/xml" }
,{ "Content-Length", "0" }
,{ "DCLK_imp", "v7;x;114750856;0-0;0;17820020;0/0;21603567/21621457/1;;~okv=;dcmt=text/xml;;~cs=o" }
}
,.body = ""
}

#define BONJOUR_MADAME_FR 8
/* The client should not merge two headers fields when the first one doesn't
* have a value.
*/
,{ .name = "bonjourmadame.fr"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.0 301 Moved Permanently\r\n"
"Date: Thu, 03 Jun 2010 09:56:32 GMT\r\n"
"Server: Apache/2.2.3 (Red Hat)\r\n"
"Cache-Control: public\r\n"
"Pragma: \r\n"
"Location: http://www.bonjourmadame.fr/\r\n"
"Vary: Accept-Encoding\r\n"
"Content-Length: 0\r\n"
"Content-Type: text/html; charset=UTF-8\r\n"
"Connection: keep-alive\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.status_code = 301
,.response_status = "Moved Permanently"
,.num_headers = 9
,.headers =
{ { "Date", "Thu, 03 Jun 2010 09:56:32 GMT" }
,{ "Server", "Apache/2.2.3 (Red Hat)" }
,{ "Cache-Control", "public" }
,{ "Pragma", "" }
,{ "Location", "http://www.bonjourmadame.fr/" }
,{ "Vary",  "Accept-Encoding" }
,{ "Content-Length", "0" }
,{ "Content-Type", "text/html; charset=UTF-8" }
,{ "Connection", "keep-alive" }
}
,.body = ""
}

#define RES_FIELD_UNDERSCORE 9
/* Should handle spaces in header fields */
,{ .name = "field underscore"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Date: Tue, 28 Sep 2010 01:14:13 GMT\r\n"
"Server: Apache\r\n"
"Cache-Control: no-cache, must-revalidate\r\n"
"Expires: Mon, 26 Jul 1997 05:00:00 GMT\r\n"
".et-Cookie: PlaxoCS=1274804622353690521; path=/; domain=.plaxo.com\r\n"
"Vary: Accept-Encoding\r\n"
"_eep-Alive: timeout=45\r\n" /* semantic value ignored */
"_onnection: Keep-Alive\r\n" /* semantic value ignored */
"Transfer-Encoding: chunked\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n"
"\r\n"
"0\r\n\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 11
,.headers =
{ { "Date", "Tue, 28 Sep 2010 01:14:13 GMT" }
,{ "Server", "Apache" }
,{ "Cache-Control", "no-cache, must-revalidate" }
,{ "Expires", "Mon, 26 Jul 1997 05:00:00 GMT" }
,{ ".et-Cookie", "PlaxoCS=1274804622353690521; path=/; domain=.plaxo.com" }
,{ "Vary", "Accept-Encoding" }
,{ "_eep-Alive", "timeout=45" }
,{ "_onnection", "Keep-Alive" }
,{ "Transfer-Encoding", "chunked" }
,{ "Content-Type", "text/html" }
,{ "Connection", "close" }
}
,.body = ""
,.num_chunks_complete = 1
,.chunk_lengths = {}
}

#define NON_ASCII_IN_STATUS_LINE 10
/* Should handle non-ASCII in status line */
,{ .name = "non-ASCII in status line"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 500 Oriëntatieprobleem\r\n"
"Date: Fri, 5 Nov 2010 23:07:12 GMT+2\r\n"
"Content-Length: 0\r\n"
"Connection: close\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 500
,.response_status = "Oriëntatieprobleem"
,.num_headers = 3
,.headers =
{ { "Date", "Fri, 5 Nov 2010 23:07:12 GMT+2" }
,{ "Content-Length", "0" }
,{ "Connection", "close" }
}
,.body = ""
}

#define HTTP_VERSION_0_9 11
/* Should handle HTTP/0.9 */
,{ .name = "http version 0.9"
,.type = HTTP_RESPONSE
,.raw = "HTTP/0.9 200 OK\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 0
,.http_minor = 9
,.status_code = 200
,.response_status = "OK"
,.num_headers = 0
,.headers =
{}
,.body = ""
}

#define NO_CONTENT_LENGTH_NO_TRANSFER_ENCODING_RESPONSE 12
/* The client should wait for the server's EOF. That is, when neither
* content-length nor transfer-encoding is specified, the end of body
* is specified by the EOF.
*/
,{ .name = "neither content-length nor transfer-encoding response"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/plain\r\n"
"\r\n"
"hello world"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 1
,.headers =
{ { "Content-Type", "text/plain" }
}
,.body = "hello world"
}

#define NO_BODY_HTTP10_KA_200 13
,{ .name = "HTTP/1.0 with keep-alive and EOF-terminated 200 status"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.0 200 OK\r\n"
"Connection: keep-alive\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 0
,.status_code = 200
,.response_status = "OK"
,.num_headers = 1
,.headers =
{ { "Connection", "keep-alive" }
}
,.body_size = 0
,.body = ""
}

#define NO_BODY_HTTP10_KA_204 14
,{ .name = "HTTP/1.0 with keep-alive and a 204 status"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.0 204 No content\r\n"
"Connection: keep-alive\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 0
,.status_code = 204
,.response_status = "No content"
,.num_headers = 1
,.headers =
{ { "Connection", "keep-alive" }
}
,.body_size = 0
,.body = ""
}

#define NO_BODY_HTTP11_KA_200 15
,{ .name = "HTTP/1.1 with an EOF-terminated 200 status"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 0
,.headers = {}
,.body_size = 0
,.body = ""
}

#define NO_BODY_HTTP11_KA_204 16
,{ .name = "HTTP/1.1 with a 204 status"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 204 No content\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 204
,.response_status = "No content"
,.num_headers = 0
,.headers = {}
,.body_size = 0
,.body = ""
}

#define NO_BODY_HTTP11_NOKA_204 17
,{ .name = "HTTP/1.1 with a 204 status and keep-alive disabled"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 204 No content\r\n"
"Connection: close\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 204
,.response_status = "No content"
,.num_headers = 1
,.headers =
{ { "Connection", "close" }
}
,.body_size = 0
,.body = ""
}

#define NO_BODY_HTTP11_KA_CHUNKED_200 18
,{ .name = "HTTP/1.1 with chunked endocing and a 200 response"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 1
,.headers =
{ { "Transfer-Encoding", "chunked" }
}
,.body_size = 0
,.body = ""
,.num_chunks_complete = 1
}

#if !HTTP_PARSER_STRICT
#define SPACE_IN_FIELD_RES 19
/* Should handle spaces in header fields */
,{ .name = "field space"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Server: Microsoft-IIS/6.0\r\n"
"X-Powered-By: ASP.NET\r\n"
"en-US Content-Type: text/xml\r\n" /* this is the problem */
"Content-Type: text/xml\r\n"
"Content-Length: 16\r\n"
"Date: Fri, 23 Jul 2010 18:45:38 GMT\r\n"
"Connection: keep-alive\r\n"
"\r\n"
"<xml>hello</xml>" /* fake body */
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 7
,.headers =
{ { "Server",  "Microsoft-IIS/6.0" }
,{ "X-Powered-By", "ASP.NET" }
,{ "en-US Content-Type", "text/xml" }
,{ "Content-Type", "text/xml" }
,{ "Content-Length", "16" }
,{ "Date", "Fri, 23 Jul 2010 18:45:38 GMT" }
,{ "Connection", "keep-alive" }
}
,.body = "<xml>hello</xml>"
}
#endif /* !HTTP_PARSER_STRICT */

#define AMAZON_COM 20
,{ .name = "amazon.com"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 301 MovedPermanently\r\n"
"Date: Wed, 15 May 2013 17:06:33 GMT\r\n"
"Server: Server\r\n"
"x-amz-id-1: 0GPHKXSJQ826RK7GZEB2\r\n"
"p3p: policyref=\"http://www.amazon.com/w3c/p3p.xml\",CP=\"CAO DSP LAW CUR ADM IVAo IVDo CONo OTPo OUR DELi PUBi OTRi BUS PHY ONL UNI PUR FIN COM NAV INT DEM CNT STA HEA PRE LOC GOV OTC \"\r\n"
"x-amz-id-2: STN69VZxIFSz9YJLbz1GDbxpbjG6Qjmmq5E3DxRhOUw+Et0p4hr7c/Q8qNcx4oAD\r\n"
"Location: http://www.amazon.com/Dan-Brown/e/B000AP9DSU/ref=s9_pop_gw_al1?_encoding=UTF8&refinementId=618073011&pf_rd_m=ATVPDKIKX0DER&pf_rd_s=center-2&pf_rd_r=0SHYY5BZXN3KR20BNFAY&pf_rd_t=101&pf_rd_p=1263340922&pf_rd_i=507846\r\n"
"Vary: Accept-Encoding,User-Agent\r\n"
"Content-Type: text/html; charset=ISO-8859-1\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"1\r\n"
"\n\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 301
,.response_status = "MovedPermanently"
,.num_headers = 9
,.headers = { { "Date", "Wed, 15 May 2013 17:06:33 GMT" }
,{ "Server", "Server" }
,{ "x-amz-id-1", "0GPHKXSJQ826RK7GZEB2" }
,{ "p3p", "policyref=\"http://www.amazon.com/w3c/p3p.xml\",CP=\"CAO DSP LAW CUR ADM IVAo IVDo CONo OTPo OUR DELi PUBi OTRi BUS PHY ONL UNI PUR FIN COM NAV INT DEM CNT STA HEA PRE LOC GOV OTC \"" }
,{ "x-amz-id-2", "STN69VZxIFSz9YJLbz1GDbxpbjG6Qjmmq5E3DxRhOUw+Et0p4hr7c/Q8qNcx4oAD" }
,{ "Location", "http://www.amazon.com/Dan-Brown/e/B000AP9DSU/ref=s9_pop_gw_al1?_encoding=UTF8&refinementId=618073011&pf_rd_m=ATVPDKIKX0DER&pf_rd_s=center-2&pf_rd_r=0SHYY5BZXN3KR20BNFAY&pf_rd_t=101&pf_rd_p=1263340922&pf_rd_i=507846" }
,{ "Vary", "Accept-Encoding,User-Agent" }
,{ "Content-Type", "text/html; charset=ISO-8859-1" }
,{ "Transfer-Encoding", "chunked" }
}
,.body = "\n"
,.num_chunks_complete = 2
,.chunk_lengths = { 1 }
}

#define EMPTY_REASON_PHRASE_AFTER_SPACE 20
,{ .name = "empty reason phrase after space"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 \r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = ""
,.num_headers = 0
,.headers = {}
,.body = ""
}

#define CONTENT_LENGTH_X 21
,{ .name = "Content-Length-X"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Content-Length-X: 0\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"2\r\n"
"OK\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 2
,.headers = { { "Content-Length-X", "0" }
,{ "Transfer-Encoding", "chunked" }
}
,.body = "OK"
,.num_chunks_complete = 2
,.chunk_lengths = { 2 }
}

#define HTTP_101_RESPONSE_WITH_UPGRADE_HEADER 22
,{ .name = "HTTP 101 response with Upgrade header"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 101 Switching Protocols\r\n"
"Connection: upgrade\r\n"
"Upgrade: h2c\r\n"
"\r\n"
"proto"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 101
,.response_status = "Switching Protocols"
,.upgrade = "proto"
,.num_headers = 2
,.headers =
{ { "Connection", "upgrade" }
,{ "Upgrade", "h2c" }
}
}

#define HTTP_101_RESPONSE_WITH_UPGRADE_HEADER_AND_CONTENT_LENGTH 23
,{ .name = "HTTP 101 response with Upgrade and Content-Length header"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 101 Switching Protocols\r\n"
"Connection: upgrade\r\n"
"Upgrade: h2c\r\n"
"Content-Length: 4\r\n"
"\r\n"
"body"
"proto"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 101
,.response_status = "Switching Protocols"
,.body = "body"
,.upgrade = "proto"
,.num_headers = 3
,.headers =
{ { "Connection", "upgrade" }
,{ "Upgrade", "h2c" }
,{ "Content-Length", "4" }
}
}

#define HTTP_101_RESPONSE_WITH_UPGRADE_HEADER_AND_TRANSFER_ENCODING 24
,{ .name = "HTTP 101 response with Upgrade and Transfer-Encoding header"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 101 Switching Protocols\r\n"
"Connection: upgrade\r\n"
"Upgrade: h2c\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"2\r\n"
"bo\r\n"
"2\r\n"
"dy\r\n"
"0\r\n"
"\r\n"
"proto"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 101
,.response_status = "Switching Protocols"
,.body = "body"
,.upgrade = "proto"
,.num_headers = 3
,.headers =
{ { "Connection", "upgrade" }
,{ "Upgrade", "h2c" }
,{ "Transfer-Encoding", "chunked" }
}
,.num_chunks_complete = 3
,.chunk_lengths = { 2, 2 }
}

#define HTTP_200_RESPONSE_WITH_UPGRADE_HEADER 25
,{ .name = "HTTP 200 response with Upgrade header"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Connection: upgrade\r\n"
"Upgrade: h2c\r\n"
"\r\n"
"body"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.body = "body"
,.upgrade = NULL
,.num_headers = 2
,.headers =
{ { "Connection", "upgrade" }
,{ "Upgrade", "h2c" }
}
}

#define HTTP_200_RESPONSE_WITH_UPGRADE_HEADER_AND_CONTENT_LENGTH 26
,{ .name = "HTTP 200 response with Upgrade and Content-Length header"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Connection: upgrade\r\n"
"Upgrade: h2c\r\n"
"Content-Length: 4\r\n"
"\r\n"
"body"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 3
,.body = "body"
,.upgrade = NULL
,.headers =
{ { "Connection", "upgrade" }
,{ "Upgrade", "h2c" }
,{ "Content-Length", "4" }
}
}

#define HTTP_200_RESPONSE_WITH_UPGRADE_HEADER_AND_TRANSFER_ENCODING 27
,{ .name = "HTTP 200 response with Upgrade and Transfer-Encoding header"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Connection: upgrade\r\n"
"Upgrade: h2c\r\n"
"Transfer-Encoding: chunked\r\n"
"\r\n"
"2\r\n"
"bo\r\n"
"2\r\n"
"dy\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = TRUE
,.message_complete_on_eof = FALSE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 3
,.body = "body"
,.upgrade = NULL
,.headers =
{ { "Connection", "upgrade" }
,{ "Upgrade", "h2c" }
,{ "Transfer-Encoding", "chunked" }
}
,.num_chunks_complete = 3
,.chunk_lengths = { 2, 2 }
}
#define HTTP_200_MULTI_TE_NOT_LAST_CHUNKED 28
,{ .name = "HTTP 200 response with `chunked` being *not last* Transfer-Encoding"
,.type = HTTP_RESPONSE
,.raw = "HTTP/1.1 200 OK\r\n"
"Transfer-Encoding: chunked, identity\r\n"
"\r\n"
"2\r\n"
"OK\r\n"
"0\r\n"
"\r\n"
,.should_keep_alive = FALSE
,.message_complete_on_eof = TRUE
,.http_major = 1
,.http_minor = 1
,.status_code = 200
,.response_status = "OK"
,.num_headers = 1
,.headers = { { "Transfer-Encoding", "chunked, identity" }
}
,.body = "2\r\nOK\r\n0\r\n\r\n"
,.num_chunks_complete = 0
}
};

}