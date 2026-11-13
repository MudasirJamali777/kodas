// HTTP client + server; import is handled in main.c via source prepending.
// Compiled into generated binary alongside krt.c.
#include "codegen/krt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <threads.h>
#include <poll.h>
#include <setjmp.h>
#include <string.h>

// k_module/k_mod_set/k_str_len/k_index_set/k_alloc are not yet public in krt.h;
// declare them extern here.
extern void* k_alloc(size_t sz);
extern KVal* k_module(const char* name);
extern void  k_mod_set(KVal* mod, const char* name, KVal* val);
extern KVal* k_str_len(const char* s, int n);
extern void  k_index_set(KVal* col, KVal* key, KVal* val);
extern KVal* k_new(KKind kind);

// ── Minimal dynamic byte buffer ─────────────────────────────────────
typedef struct { char* buf; int len; int cap; } Buf;
static void buf_init(Buf* b){b->buf=NULL;b->len=0;b->cap=0;}
static void buf_grow(Buf* b,int n){while(b->len+n+1>b->cap)b->cap=b->cap?b->cap*2:256;b->buf=(char*)realloc(b->buf,b->cap);}
static void buf_append(Buf* b,const char*s,int n){buf_grow(b,n);memcpy(b->buf+b->len,s,n);b->len+=n;b->buf[b->len]=0;}
static void buf_appends(Buf* b,const char*s){buf_append(b,s,(int)strlen(s));}
static char* buf_take(Buf* b){char*r=b->buf;b->buf=NULL;b->len=b->cap=0;return r;}

// ── HTTP client: http.get(url) -> {status, body, headers} ──────────
typedef struct { int code; char* body; int body_len; } HTTPResp;
static int http_request(const char* method, const char* url, const char* body, char** out_body, int* out_len, int* out_code, char** err_out) {
    // Parse URL: http://host[:port]/path
    const char* p = url;
    if (strncmp(p,"http://",7)!=0){*err_out=(char*)"only http:// URLs supported";return -1;}
    p+=7;
    const char* slash = strchr(p,'/');
    char host[256]=""; int port=80;
    const char* path = "/";
    if (slash){int n=(int)(slash-p);if(n>=256)n=255;memcpy(host,p,n);host[n]=0;path=slash;}
    else{int n=(int)strlen(p);if(n>=256)n=255;memcpy(host,p,n);host[n]=0;}
    // Check for :port
    char* colon = strchr(host,':');
    if (colon){*colon=0;port=atoi(colon+1);}
    struct addrinfo hints={0},*res;
    hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    char port_str[8];snprintf(port_str,sizeof(port_str),"%d",port);
    if (getaddrinfo(host,port_str,&hints,&res)!=0){*err_out=(char*)"dns lookup failed";return -1;}
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if (fd<0){freeaddrinfo(res);*err_out=(char*)"socket failed";return -1;}
    if (connect(fd,res->ai_addr,res->ai_addrlen)<0){close(fd);freeaddrinfo(res);*err_out=(char*)"connect failed";return -1;}
    freeaddrinfo(res);
    // Build request
    Buf req;buf_init(&req);
    buf_appends(&req,method);buf_append(&req," ",1);buf_appends(&req,path);buf_appends(&req," HTTP/1.0\r\nHost: ");buf_appends(&req,host);buf_appends(&req,"\r\n");
    if(body){char cl[32];snprintf(cl,sizeof(cl),"Content-Length: %zu\r\n",strlen(body));buf_appends(&req,cl);}
    buf_appends(&req,"Connection: close\r\n\r\n");
    if(body)buf_appends(&req,body);
    ssize_t wr=write(fd,req.buf,req.len);(void)wr;
    free(req.buf);
    // Read response
    Buf rb;buf_init(&rb);char tmp[4096];ssize_t n;
    while((n=read(fd,tmp,sizeof(tmp)))>0)buf_append(&rb,tmp,(int)n);
    close(fd);
    // Parse: split at \r\n\r\n
    char* hdr_end = rb.buf?strstr(rb.buf,"\r\n\r\n"):NULL;
    int code=0;int body_off=rb.len;
    if(hdr_end){
        *hdr_end=0;
        // Find status line "HTTP/1.X NNN ..."
        char* sp = strchr(rb.buf,' ');
        if(sp){sp++;code=atoi(sp);}
        body_off=(int)(hdr_end-rb.buf)+4;
    }
    *out_code=code;
    *out_len=rb.len-body_off;
    *out_body=(char*)malloc(*out_len+1);
    memcpy(*out_body,rb.buf+body_off,*out_len);(*out_body)[*out_len]=0;
    free(rb.buf);
    return 0;
}
static KVal* b_http_get(int argc,KVal**argv){
    if(argc<1||argv[0]->kind!=K_STRING)k_throw(k_err(k_str("http.get(url)")));
    char*body=NULL;int bl=0,code=0;char*err=NULL;
    int rc=http_request("GET",argv[0]->as.string_v,NULL,&body,&bl,&code,&err);
    if(rc!=0){if(body)free(body);k_throw(k_err(k_str(err?err:"http.get failed")));return k_null();}
    KVal* resp=k_map_empty();
    k_index_set(resp,k_str("status"),k_int(code));
    k_index_set(resp,k_str("body"),k_str_len(body,bl));
    free(body);
    return resp;
}
static KVal* b_http_post(int argc,KVal**argv){
    if(argc<2||argv[0]->kind!=K_STRING||argv[1]->kind!=K_STRING)k_throw(k_err(k_str("http.post(url, body)")));
    char*body=NULL;int bl=0,code=0;char*err=NULL;
    int rc=http_request("POST",argv[0]->as.string_v,argv[1]->as.string_v,&body,&bl,&code,&err);
    if(rc!=0){if(body)free(body);k_throw(k_err(k_str(err?err:"http.post failed")));return k_null();}
    KVal* resp=k_map_empty();
    k_index_set(resp,k_str("status"),k_int(code));
    k_index_set(resp,k_str("body"),k_str_len(body,bl));
    free(body);
    return resp;
}

// ── HTTP server: http.listen(port, fn(req) -> resp) ─────────────────
static KVal* srv_handler = NULL;
static void handle_conn(int client) {
    extern __thread KFrame* k_current_frame;
    KFrame fr; memset(&fr,0,sizeof(fr));
    fr.prev = k_current_frame; k_current_frame = &fr;
    if (setjmp(fr.buf)) {
        if (fr.error) { fprintf(stderr,"[http] handler err\n"); k_release(fr.error); fr.error=NULL; }
        const char* err500="HTTP/1.0 500\r\nContent-Length:0\r\nConnection:close\r\n\r\n";
        (void)!write(client,err500,strlen(err500));
        close(client); k_current_frame=fr.prev; return;
    }
    Buf rb;buf_init(&rb);char tmp[4096];
    // Read headers until \r\n\r\n, up to 64KB
    int total=0;
    while(total<65536){
        ssize_t n=read(client,tmp,sizeof(tmp));
        if(n<=0)break;
        buf_append(&rb,tmp,(int)n);total+=(int)n;
        if(strstr(rb.buf,"\r\n\r\n"))break;
    }
    if(!rb.buf||!strstr(rb.buf,"\r\n\r\n")){close(client);free(rb.buf);return;}
    char* hdr_end = strstr(rb.buf,"\r\n\r\n");
    *hdr_end=0;char*headers_start=rb.buf;
    int body_off=(int)(hdr_end-rb.buf)+4;
    int body_read = total-body_off;
    // Parse method/path from first line
    char method[16]="GET",path[1024]="/";
    sscanf(headers_start,"%15s %1023s",method,path);
    // Build req map
    KVal* req = k_map_empty();
    k_index_set(req,k_str("method"),k_str(method));
    k_index_set(req,k_str("path"),k_str(path));
    k_index_set(req,k_str("body"),k_str_len(rb.buf+body_off,body_read));
    k_index_set(req,k_str("headers"),k_map_empty());
    // Call handler
    KVal* args[1]={req};
    KVal* resp = k_call(srv_handler,1,args);
    k_release(req);
    // Build response
    int status = 200;
    const char* rbody = ""; int rblen = 0;
    const char* ct = "text/plain; charset=utf-8";
    if(resp && resp->kind==K_MAP){
        KMap* m = resp->as.map;
        KVal* body_v=NULL;
        for(int i=0;i<m->cap;i++){
            for(KMapEntry*e=m->buckets[i];e;e=e->next){
                if(e->key->kind==K_STRING){
                    if(strcmp(e->key->as.string_v,"status")==0&&e->val->kind==K_INT)status=(int)e->val->as.int_v;
                    else if(strcmp(e->key->as.string_v,"body")==0)body_v=e->val;
                    else if(strcmp(e->key->as.string_v,"content_type")==0&&e->val->kind==K_STRING)ct=e->val->as.string_v;
                }
            }
        }
        if(body_v&&body_v->kind==K_STRING){rbody=body_v->as.string_v;rblen=(int)strlen(rbody);}
    }
    char out[8192];int ol=snprintf(out,sizeof(out),
        "HTTP/1.0 %d OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        status,ct,rblen);
    close(client);
    if(resp)k_release(resp);
    free(rb.buf);
}
static int server_accept_thread(void* arg) {
    int srv_fd=(int)(intptr_t)arg;
    while(1){
        struct sockaddr_in ca;socklen_t cl=sizeof(ca);
        int c=accept(srv_fd,(struct sockaddr*)&ca,&cl);
        if(c<0)break;
        handle_conn(c);
    }
    return 0;
}
static KVal* b_http_response(int argc,KVal**argv){
    int status=200; const char*body=""; const char*ct="text/plain; charset=utf-8";
    if(argc>=1&&argv[0]->kind==K_INT)status=(int)argv[0]->as.int_v;
    if(argc>=2&&argv[1]->kind==K_STRING)body=argv[1]->as.string_v;
    if(argc>=3&&argv[2]->kind==K_STRING)ct=argv[2]->as.string_v;
    KVal* r=k_map_empty();
    k_index_set(r,k_str("status"),k_int(status));
    k_index_set(r,k_str("body"),k_str(body));
    k_index_set(r,k_str("content_type"),k_str(ct));
    return r;
}
static KVal* b_http_listen(int argc,KVal**argv){
    if(argc<2||argv[0]->kind!=K_INT||argv[1]->kind!=K_CLOSURE&&argv[1]->kind!=K_NATIVE)
        k_throw(k_err(k_str("http.listen(port, handler)")));
    int port=(int)argv[0]->as.int_v;
    srv_handler = k_retain(argv[1]);
    int fd=socket(AF_INET,SOCK_STREAM,0);
    int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(port);
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);k_throw(k_err(k_str("bind failed")));return k_null();}
    listen(fd,16);
    thrd_t t;thrd_create(&t,server_accept_thread,(void*)(intptr_t)fd);thrd_detach(t);
    return k_ok(k_int(port));
}

// ── Registration ────────────────────────────────────────────────────
static KNative_* mk_native(KNativeFn fn,const char*nm){
    KNative_*n=(KNative_*)k_alloc(sizeof(KNative_));n->fn=fn;n->name=nm;return n;
}
static KVal* mk_nv(KNativeFn fn,const char*nm){KVal*v=k_new(K_NATIVE);v->as.native=mk_native(fn,nm);return v;}
void k_register_stdlib_http(void) {
    KVal* m = k_module("http");
    k_mod_set(m,"get",mk_nv(b_http_get,"http.get"));
    k_mod_set(m,"post",mk_nv(b_http_post,"http.post"));
    k_mod_set(m,"listen",mk_nv(b_http_listen,"http.listen"));
    k_mod_set(m,"response",mk_nv(b_http_response,"http.response"));
    k_global_set("http",m);k_release(m);
}
