enum idl_type_id {
	idl_null = -1,
	idl_bool = -2,
	idl_nat = -3,
	idl_int = -4,
	idl_nat8 = -5,
	idl_nat16 = -6,
	idl_nat32 = -7,
	idl_nat64 = -8,
	idl_int8 = -9,
	idl_int16 = -10,
	idl_int32 = -11,
	idl_int64 = -12,
	idl_float32 = -13,
	idl_float64 = -14,
	idl_text = -15,
	idl_reserved = -16,
	idl_empty = -17,
	idl_opt = -18,
	idl_vector = -19,
	idl_record = -20,
	idl_variant = -21,
	idl_func = -22,
	idl_service = -23,
	idl_principal = -24,
};
#define str_bytes(s) s, strlen(s)
unsigned int idl_hash_name(const char* name);
int idl_create(unsigned char* buf, int type_count, int arg_count, ...);
int idl_write_nat(unsigned char* buf, unsigned long long v);
int idl_write_int(unsigned char* buf, long long v);
int idl_write_bytes(unsigned char* buf, const void* data, int len);
int idl_write_text(unsigned char* buf, const char* s);
int idl_read_nat(unsigned char* buf, unsigned int* pv);
int idl_read_int(unsigned char* buf, int* pv);
unsigned char* idl_get_arg(unsigned char* idl, int idx, int* ptype);
unsigned char* idl_vector_get(unsigned char* buf, int idx, int* ptype, unsigned char* idl);
unsigned char* idl_variant_get(unsigned char* buf, int* ptype, unsigned char* idl);
unsigned char* idl_record_get(unsigned char* buf, unsigned int field, int* ptype, unsigned char* idl);
unsigned char* idl_get_bytes(unsigned char* buf, int* plen);
unsigned char* ic_call(const char* canister_id, const char* method_name, unsigned char* idl_buf, int* pidl_len, const char* request_type);
//user implements
extern void* ic_http_post(const char* path, void* data, int* plen);
extern int ic_calc_sha256(unsigned char* buf, const unsigned char* data, int len);
extern void ic_sleep(int n);
extern void ic_error();
