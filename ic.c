#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ic.h"

static int sleb_write(unsigned char* buf, long long v) {
	unsigned char* p = buf;
	for (;;) {
		unsigned char b = v & 0x7F;
		v >>= 7;
		if ((v == 0 && (b & 0x40) == 0) || (v == -1 && (b & 0x40) != 0)) {
			*p++ = b;
			return p - buf;
		}
		*p++ = 0x80 | b;
	}
}
static int uleb_write(unsigned char* buf, unsigned long long v) {
	unsigned char* p = buf;
	for (;;) {
		unsigned char b = v & 0x7F;
		v >>= 7;
		if (v == 0) {
			*p++ = b;
			return p - buf;
		}
		*p++ = 0x80 | b;
	}
}
static int sleb_read(unsigned char* buf, long long *pv) {
	unsigned char* p = buf;
	long long value = 0;
	unsigned char b;
	int i;
	for (i = 0;; ++i) {
		b = *p++;
		value += (long long)(b & 0x7F) << (i * 7);
		if ((b & 0x80) == 0)
			break;
	}
	if ((b & 0x40) != 0)
		value |= -((long long)1 << ((i * 7) + 7));
	if (pv) *pv = value;
	return p - buf;
}
static int uleb_read(unsigned char* buf, unsigned long long *pv) {
	unsigned char* p = buf;
	unsigned long long value = 0;
	unsigned long long weight = 1;
	unsigned char b;
	do {
		b = *p++;
		value += (b & 0x7F) * weight;
		weight *= 0x80;
	} while (b >= 0x80);
	if (pv) *pv = value;
	return p - buf;
}
unsigned int idl_hash_name(const char* name) {
	unsigned int h = 0;
	while (*name)
		h = h * 223 + (unsigned char)*name++;
	return h;
}
int idl_write_nat(unsigned char* buf, unsigned long long v) {
	return uleb_write(buf, v);
}
int idl_write_int(unsigned char* buf, long long v) {
	return sleb_write(buf, v);
}
int idl_write_bytes(unsigned char* buf, const void* data, int len) {
	memmove(buf, data, len);
	return len;
}
int idl_write_text(unsigned char* buf, const char* s) {
	unsigned char* p = buf;
	unsigned int len = strlen(s);
	p += idl_write_nat(p, len);
	p += idl_write_bytes(p, s, len);
	return p - buf;
}
int idl_create(unsigned char* buf, int type_count, int arg_count, ...) {
	va_list vl;
	va_start(vl, arg_count);
	unsigned char* p = buf;
	p += idl_write_bytes(p, "DIDL", 4);
	p += idl_write_int(p, type_count);
	for (int i = 0; i < type_count; ++i) {
		int type = va_arg(vl, int);
		p += idl_write_int(p, type);
		if (type == idl_opt || type == idl_vector) {
			type = va_arg(vl, int);
			p += idl_write_int(p, type);
		}
		else if (type == idl_record || type == idl_variant) {
			int n = va_arg(vl, int);
			p += idl_write_nat(p, n);
			unsigned int prev_name_id = 0;
			for (int i = 0; i < n; ++i) {
				unsigned int name_id = va_arg(vl, unsigned int);
				if (name_id < prev_name_id)
					ic_error();
				prev_name_id = name_id;
				p += idl_write_nat(p, name_id);
				p += idl_write_int(p, va_arg(vl, int));
			}
		}
		else ic_error();
	}
	p += idl_write_int(p, arg_count);
	for (int i = 0; i < arg_count; ++i) {
		p += idl_write_int(p, va_arg(vl, int));
	}
	return p - buf;
}
int idl_read_nat64(unsigned char* buf, unsigned long long* pv) {
	return uleb_read(buf, pv);
}
int idl_read_int64(unsigned char* buf, long long* pv) {
	return sleb_read(buf, pv);
}
int idl_read_nat(unsigned char* buf, unsigned int* pv) {
	unsigned long long v;
	int n = idl_read_nat64(buf, &v);
	if (pv) *pv = (unsigned int)v;
	return n;
}
int idl_read_int(unsigned char* buf, int* pv) {
	long long v;
	int n = idl_read_int64(buf, &v);
	if (pv) *pv = (int)v;
	return n;
}
unsigned char* idl_get_bytes(unsigned char* buf, int* plen) {
	unsigned char* p = buf;
	p += idl_read_nat(p, plen);
	return p;
}
static unsigned char* idl_get_type(unsigned char* idl, int idx) {
	unsigned char* p = idl + 4;
	int n;
	p += idl_read_nat(p, &n);
	if (idx == -1) idx = n;
	for (int i = 0; i < idx; ++i) {
		int type;
		p += idl_read_int(p, &type);
		if (type == idl_opt || type == idl_vector) {
			p += idl_read_int(p, 0);
		}
		else if (type == idl_record || type == idl_variant) {
			p += idl_read_nat(p, &n);
			for (int i = 0; i < n; ++i) {
				p += idl_read_nat(p, 0);
				p += idl_read_int(p, 0);
			}
		}
		else if (type == idl_func) {
			p += idl_read_nat(p, &n);	//args
			for (int i = 0; i < n; ++i)
				p += idl_read_int(p, 0);
			p += idl_read_nat(p, &n);	//rets
			for (int i = 0; i < n; ++i)
				p += idl_read_int(p, 0);
			p += idl_read_nat(p, &n);	//anns
			p += n;
		}
		else ic_error();
	}
	return p;
}
static unsigned char* idl_read_skip(unsigned char* buf, int type, unsigned char* idl) {
	unsigned char* p = buf;
	if (type >= 0) {
		unsigned char* pt = idl_get_type(idl, type);
		pt += idl_read_int(pt, &type);
	}
	if (type == idl_nat) {
		p += idl_read_nat(p, 0);
	}
	else if (type == idl_int) {
		p += idl_read_int(p, 0);
	}
	else if (type == idl_nat8 || type == idl_int8 || type == idl_bool) {
		p += 1;
	}
	else if (type == idl_nat16 || type == idl_int16) {
		p += 2;
	}
	else if (type == idl_nat32 || type == idl_int32 || type == idl_float32) {
		p += 4;
	}
	else if (type == idl_nat64 || type == idl_int64 || type == idl_float64) {
		p += 8;
	}
	else if (type == idl_text) {
		int len;
		p = idl_get_bytes(p, &len);
		p += len;
	}
	else if (type == idl_opt || type == idl_vector) {
		p = idl_vector_get(p, -1, &type, idl);
	}
	else if (type == idl_record) {
		p = idl_record_get(p, -1, &type, idl);
	}
	else if (type == idl_variant) {
		p = idl_variant_get(p, &type, idl);
		p = idl_read_skip(p, type, idl);
	}
	else ic_error();
	return p;
}
unsigned char* idl_record_get(unsigned char* buf, unsigned int field, int* ptype, unsigned char* idl) {
	unsigned char* p = buf;
	unsigned char* pt = idl_get_type(idl, *ptype);
	pt += idl_read_int(pt, 0);
	int n;
	pt += idl_read_nat(pt, &n);
	for (int i = 0; i < n; ++i) {
		unsigned int id;
		pt += idl_read_nat(pt, &id);
		pt += idl_read_int(pt, ptype);
		if (field == id)
			break;
		p = idl_read_skip(p, *ptype, idl);
	}
	return p;
}
unsigned char* idl_variant_get(unsigned char* buf, int* ptype, unsigned char* idl) {
	unsigned char* p = buf;
	unsigned char* pt = idl_get_type(idl, *ptype);
	pt += idl_read_int(pt, 0);
	pt += idl_read_nat(pt, 0);
	int idx;
	p += idl_read_nat(p, &idx);
	do {
		pt += idl_read_nat(pt, 0);
		pt += idl_read_int(pt, ptype);
	} while (idx--);
	return p;
}
unsigned char* idl_vector_get(unsigned char* buf, int idx, int* ptype, unsigned char* idl) {
	unsigned char* p = buf;
	unsigned char* pt = idl_get_type(idl, *ptype);
	pt += idl_read_int(pt, 0);
	pt += idl_read_int(pt, ptype);
	unsigned int len;
	p += idl_read_nat(p, &len);
	if (idx == -1) idx = len;
	for (int i = 0; i < idx; ++i)
		p = idl_read_skip(p, *ptype, idl);
	return p;
}
unsigned char* idl_get_arg(unsigned char* idl, int idx, int* ptype) {
	unsigned char* pl = idl_get_type(idl, -1);;
	int n;
	pl += idl_read_nat(pl, &n);
	unsigned char* p = pl;
	for (int i = 0; i < n; ++i) {
		p += idl_read_int(p, 0);
	}
	if (idx == -1) idx = n;
	for (int i = 0; i < idx; ++i) {
		pl += idl_read_int(pl, ptype);
		p = idl_read_skip(p, *ptype, idl);
	}
	idl_read_int(pl, ptype);
	return p;
}
enum cbor_type {
	cbor_unsigned,
	cbor_negative,
	cbor_bytes,
	cbor_text,
	cbor_array,
	cbor_map,
	cbor_tag,
	cbor_prim
};
static void cbor_bswap(void* buf, int n) {
	unsigned char* p = buf;
	for (int i = 0; i < n / 2; ++i) {
		unsigned char b = p[i];
		p[i] = p[n - 1 - i];
		p[n - 1 - i] = b;
	}
}
static int cbor_write(unsigned char* buf, unsigned char type, unsigned long long n) {
	unsigned char* p = buf;
	if (n <= 0x17) {
		*p++ = (type << 5) + (unsigned char)n;
	}
	else if (n <= 0xFF) {
		*p++ = (type << 5) + 0x18;
		*p++ = (unsigned char)n;
	}
	else if (n <= 0xFFFF) {
		*p++ = (type << 5) + 0x19;
		memmove(p, &n, 2);
		cbor_bswap(p, 2);
		p += 2;
	}
	else if (n <= 0xFFFFFFFF) {
		*p++ = (type << 5) + 0x1A;
		memmove(p, &n, 4);
		cbor_bswap(p, 4);
		p += 4;
	}
	else {
		*p++ = (type << 5) + 0x1B;
		memmove(p, &n, 8);
		cbor_bswap(p, 8);
		p += 8;
	}
	return p - buf;
}
static int cbor_write_int(unsigned char* buf, long long n) {
	if (n >= 0)
		return cbor_write(buf, cbor_unsigned, n);
	else
		return cbor_write(buf, cbor_negative, -1 - n);
}
static int cbor_write_text(unsigned char* buf, const char* s) {
	unsigned char* p = buf;
	int len = strlen(s);
	p += cbor_write(p, cbor_text, len);
	memmove(p, s, len);
	p += len;
	return p - buf;
}
static int cbor_write_bytes(unsigned char* buf, const void* data, int len) {
	unsigned char* p = buf;
	p += cbor_write(p, cbor_bytes, len);
	memmove(p, data, len);
	p += len;
	return p - buf;
}
static int cbor_read(const unsigned char* buf, unsigned char *ptype, unsigned long long* pv) {
	const unsigned char* p = buf;
	if (ptype) *ptype = *p >> 5;
	if (pv) *pv = 0;
	unsigned char n = *p++ & 0x1F;
	if (n <= 0x17) {
		if (pv) *pv = n;
	}
	else if (n == 0x18) {
		if (pv) *pv = *p;
		++p;
	}
	else if (n == 0x19) {
		if (pv) {
			memmove(pv, p, 2);
			cbor_bswap(pv, 2);
		}
		p += 2;
	}
	else if (n == 0x1A) {
		if (pv) {
			memmove(pv, p, 4);
			cbor_bswap(pv, 4);
		}
		p += 4;
	}
	else if (n == 0x1B) {
		if (pv) {
			memmove(pv, p, 8);
			cbor_bswap(pv, 8);
		}
		p += 8;
	}
	return p - buf;
}
static unsigned char* cbor_get_bytes(unsigned char* buf, int* plen) {
	unsigned char t;
	unsigned long long n;
	unsigned char* p = buf;
	p += cbor_read(p, &t, &n);
	if (plen) *plen = (int)n;
	return p;
}
static int cbor_read_int(unsigned char* buf, long long* pv) {
	unsigned char* p = buf;
	unsigned char t;
	p += cbor_read(p, &t, pv);
	if (t == cbor_negative)
		*pv = -1 - *pv;
	return p - buf;
}
static int cbor_read_skip(unsigned char* buf) {
	unsigned char* p = buf;
	unsigned char t;
	unsigned long long n;
	p += cbor_read(p, &t, &n);
	if (t == cbor_bytes || t == cbor_text)
		p += n;
	else if (t == cbor_array) {
		while (n--)
			p += cbor_read_skip(p);
	}
	else if (t == cbor_map) {
		while (n--) {
			p += cbor_read_skip(p);
			p += cbor_read_skip(p);
		}
	}
	return p - buf;
}
static unsigned char* cbor_map_get(unsigned char* buf, const char* key) {
	unsigned char* p = buf;
	unsigned char t;
	unsigned long long n;
	p += cbor_read(p, &t, &n);
	while (n--) {
		if (memcmp(cbor_get_bytes(p, 0), str_bytes(key)) == 0) {
			p += cbor_read_skip(p);
			break;
		}
		p += cbor_read_skip(p);
		p += cbor_read_skip(p);
	}
	return p;
}
static unsigned char* cbor_array_get(unsigned char* buf, int key) {
	unsigned char* p = buf;
	p += cbor_read(p, 0, 0);
	while (key--)
		p += cbor_read_skip(p);
	return p;
}
static int base32_decode(const char* s, unsigned char* buf) {
	unsigned char skip = 0;
	unsigned char byte = 0;
	const char* table = "abcdefghijklmnopqrstuvwxyz234567";
	unsigned char* p = buf;
	for (; *s; ++s) {
		char* pc = strchr(table, *s);
		if (pc == 0)
			continue;
		unsigned int val = pc - table;
		val <<= 3;
		byte |= val >> skip;
		skip += 5;
		if (skip >= 8) {
			*p++ = byte;
			skip -= 8;
			byte = skip > 0 ? (val << (5 - skip)) : 0;
		}
	}
	return p - buf;
}
static unsigned char* ic_tree_get(unsigned char* buf, const unsigned char* key, int len) {
	unsigned char* p = buf;
	unsigned long long v;
	cbor_read(cbor_array_get(buf, 0), 0, &v);
	if (v == 1) {
		p = ic_tree_get(cbor_array_get(buf, 1), key, len);
		if (p) return p;
		return ic_tree_get(cbor_array_get(buf, 2), key, len);
	}
	else if (v == 2) {
		p = cbor_array_get(buf, 1);
		p += cbor_read(p, 0, &v);
		if (memcmp(p, key, len) == 0) {
			return cbor_array_get(buf, 2);
		}
	}
	return 0;
}
static unsigned char* ic_tree_value(unsigned char* buf) {
	return cbor_array_get(buf, 1);
}
static int ic_hash_bytes(unsigned char* buf, const unsigned char* data, int len) {
	return ic_calc_sha256(buf, data, len);
}
static int ic_hash_text(unsigned char* buf, const char* s) {
	return ic_hash_bytes(buf, s, strlen(s));
}
static int ic_hash_nat(unsigned char* buf, unsigned long long v) {
	int len = uleb_write(buf, v);
	return ic_hash_bytes(buf, buf, len);
}
unsigned char* ic_call(const char* canister_id, const char* method_name, unsigned char* idl_buf, int* pidl_len, const char* request_type) {
	unsigned char canister[14];
	int canister_len = base32_decode(canister_id, canister);
	memmove(canister, canister + 4, canister_len -= 4);
	long long expiry_time = (long long)(time(0) + 120) * 1000000000;
	static unsigned char* cbor_buf = 0;
	cbor_buf = realloc(cbor_buf, *pidl_len + 256);
	unsigned char* p = cbor_buf;
	p += cbor_write(p, cbor_tag, 55799);
	p += cbor_write(p, cbor_map, 1);
	p += cbor_write_text(p, "content");
	p += cbor_write(p, cbor_map, 6);
	p += cbor_write_text(p, "sender");
	p += cbor_write_bytes(p, "\x04", 1);
	p += cbor_write_text(p, "ingress_expiry");
	p += cbor_write_int(p, expiry_time);
	p += cbor_write_text(p, "method_name");
	p += cbor_write_text(p, method_name);
	p += cbor_write_text(p, "canister_id");
	p += cbor_write_bytes(p, canister, canister_len);
	p += cbor_write_text(p, "request_type");
	p += cbor_write_text(p, request_type);
	p += cbor_write_text(p, "arg");
	p += cbor_write_bytes(p, idl_buf, *pidl_len);
	int cbor_len = p - cbor_buf;
	char path[64];
	sprintf(path, "/api/v2/canister/%s/%s", canister_id, request_type);
	cbor_buf = ic_http_post(path, cbor_buf, &cbor_len);
	if (strcmp(request_type, "query") == 0) {
		p = cbor_buf;
		p += cbor_read(p, 0, 0);	//skip cbor tag
		char* status = cbor_get_bytes(cbor_map_get(p, "status"), 0);
		idl_buf = 0, * pidl_len = 0;
		if (memcmp(status, str_bytes("replied")) == 0) {
			idl_buf = cbor_get_bytes(cbor_map_get(cbor_map_get(p, "reply"), "arg"), pidl_len);
		}
		else ic_error();
	}
	else if (!strcmp(request_type, "call")) {
		unsigned char hash_buf[32 * 12];
		p = hash_buf;
		p += ic_hash_text(p, "sender");
		p += ic_hash_bytes(p, "\x04", 1);
		p += ic_hash_text(p, "canister_id");
		p += ic_hash_bytes(p, canister, canister_len);
		p += ic_hash_text(p, "ingress_expiry");
		p += ic_hash_nat(p, expiry_time);
		p += ic_hash_text(p, "method_name");
		p += ic_hash_text(p, method_name);
		p += ic_hash_text(p, "request_type");
		p += ic_hash_text(p, request_type);
		p += ic_hash_text(p, "arg");
		p += ic_hash_bytes(p, idl_buf, *pidl_len);
		p = hash_buf;
		p += ic_hash_bytes(p, hash_buf, sizeof(hash_buf));
		while (1) {
			ic_sleep(2);
			request_type = "read_state";
			cbor_buf = realloc(cbor_buf, 256);
			p = cbor_buf;
			p += cbor_write(p, cbor_tag, 55799);
			p += cbor_write(p, cbor_map, 1);
			p += cbor_write_text(p, "content");
			p += cbor_write(p, cbor_map, 4);
			p += cbor_write_text(p, "sender");
			p += cbor_write_bytes(p, "\x04", 1);
			p += cbor_write_text(p, "ingress_expiry");
			p += cbor_write_int(p, expiry_time);
			p += cbor_write_text(p, "paths");
			p += cbor_write(p, cbor_array, 1);
			p += cbor_write(p, cbor_array, 2);
			p += cbor_write_bytes(p, str_bytes("request_status"));
			p += cbor_write_bytes(p, hash_buf, 32);
			p += cbor_write_text(p, "request_type");
			p += cbor_write_text(p, request_type);
			cbor_len = p - cbor_buf;
			sprintf(path, "/api/v2/canister/%s/%s", canister_id, request_type);
			cbor_buf = ic_http_post(path, cbor_buf, &cbor_len);
			p = cbor_buf;
			p += cbor_read(p, 0, 0);	//skip cbor tag
			p = cbor_get_bytes(cbor_map_get(p, "certificate"), 0);
			p += cbor_read(p, 0, 0);	//skip cbor tag
			p = cbor_map_get(p, "tree");
			p = ic_tree_get(p, str_bytes("request_status"));
			if (!p) continue;
			p = ic_tree_get(p, hash_buf, 32);
			char* status = cbor_get_bytes(ic_tree_value(ic_tree_get(p, str_bytes("status"))), 0);
			idl_buf = 0, * pidl_len = 0;
			if (memcmp(status, str_bytes("replied")) == 0) {
				idl_buf = cbor_get_bytes(ic_tree_value(ic_tree_get(p, str_bytes("reply"))), pidl_len);
				break;
			}
			else ic_error();
		}
	}
	return idl_buf;
}
