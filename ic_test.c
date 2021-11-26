#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>
#include "ic.h"
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wininet.lib")

void* ic_http_post(const char* path, void* data, int* plen) {
	HINTERNET hInternet = InternetOpenA(NULL, 0, NULL, NULL, 0);
	HINTERNET hConnect = InternetConnectA(hInternet, "ic0.app", 443, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
	HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", path, HTTP_VERSIONA, NULL, NULL, INTERNET_FLAG_SECURE, 0);
	HttpSendRequestA(hRequest, str_bytes("content-type: application/cbor"), data, *plen);
	DWORD size = sizeof(DWORD);
	HttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, plen, &size, NULL);
	data = realloc(data, *plen);
	InternetReadFile(hRequest, data, *plen, plen);
	InternetCloseHandle(hRequest);
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);
	return data;
}
int ic_calc_sha256(unsigned char* buf, const unsigned char* data, int len) {
	DWORD cb = 32;
	CryptHashCertificate(0, CALG_SHA_256, 0, data, len, buf, &cb);
	return cb;
}
void ic_sleep(int n) {
	Sleep(n * 1000);
}
void ic_error() {
	__debugbreak();
}
static void test() {
	const char* canister_id = "h5aet-waaaa-aaaab-qaamq-cai";
	const char* method_name = "http_request";
	unsigned char buf[256];
	unsigned char* p = buf;
	p += idl_create(p, 4, 1,
		idl_record, 2, 0, idl_text, 1, idl_text,	//0: HeaderField
		idl_vector, 0,	//1: vector<HeaderField>
		idl_vector, idl_nat8,	//2: vector<nat8>
		idl_record, 4, idl_hash_name("url"), idl_text, idl_hash_name("method"), idl_text, idl_hash_name("body"), 2, idl_hash_name("headers"), 1, //3: HttpRequest
		3);	//args
	p += idl_write_text(p, "/");
	p += idl_write_text(p, "get");
	p += idl_write_nat(p, 0);
	p += idl_write_nat(p, 0);
	int len = p - buf;
	unsigned char* idl = ic_call(canister_id, method_name, buf, &len, "query");
	int type;
	p = idl_get_arg(idl, 0, &type);
	unsigned char* body = idl_get_bytes(idl_record_get(p, idl_hash_name("body"), &type, idl), &len);
	printf("%.*s\n", len, body);
}
int main() {
	test();
	getchar();
}
