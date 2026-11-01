// Extra stdlib modules: fs, json, thread/chan.
// Compiled into the generated binary alongside krt.c.
#include "codegen/krt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <threads.h>
#include <time.h>

// k_module/k_mod_set/k_alloc/k_str_len/k_index_set declared in krt.h.

// ── fs module ────────────────────────────────────────────────────────
static KVal* b_fs_read(int argc, KVal** argv) {
    if (argc < 1 || argv[0]->kind != K_STRING) k_throw(k_err(k_str("fs.read_file(path)")));
    const char* path = argv[0]->as.string_v;
    FILE* f = fopen(path, "rb");
    if (!f) { char buf[256]; snprintf(buf,sizeof(buf),"fs.read_file: %s: %s",path,strerror(errno)); k_throw(k_err(k_str(buf))); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(sz+1); size_t rd = fread(buf,1,sz,f); buf[rd]=0;
    fclose(f);
    KVal* r = k_str_len(buf, (int)rd);
    free(buf);
    return r;
}
static KVal* b_fs_write(int argc, KVal** argv) {
    if (argc < 2 || argv[0]->kind != K_STRING || argv[1]->kind != K_STRING)
        k_throw(k_err(k_str("fs.write_file(path, str)")));
    FILE* f = fopen(argv[0]->as.string_v, "wb");
    if (!f) { char buf[256]; snprintf(buf,sizeof(buf),"fs.write_file: %s",strerror(errno)); k_throw(k_err(k_str(buf))); }
    fwrite(argv[1]->as.string_v, 1, strlen(argv[1]->as.string_v), f); fclose(f);
    return k_ok(k_null());
}
static KVal* b_fs_append(int argc, KVal** argv) {
    if (argc < 2 || argv[0]->kind != K_STRING || argv[1]->kind != K_STRING)
        k_throw(k_err(k_str("fs.append_file(path, str)")));
    FILE* f = fopen(argv[0]->as.string_v, "ab");
    if (!f) k_throw(k_err(k_str("fs.append_file: open failed")));
    fwrite(argv[1]->as.string_v, 1, strlen(argv[1]->as.string_v), f); fclose(f);
    return k_ok(k_null());
}
static KVal* b_fs_exists(int argc, KVal** argv) {
    if (argc < 1 || argv[0]->kind != K_STRING) k_throw(k_err(k_str("fs.exists(path)")));
    struct stat st; return k_bool(stat(argv[0]->as.string_v, &st) == 0);
}
static KVal* b_fs_remove(int argc, KVal** argv) {
    if (argc < 1 || argv[0]->kind != K_STRING) k_throw(k_err(k_str("fs.remove(path)")));
    if (unlink(argv[0]->as.string_v) != 0) {
        char buf[256]; snprintf(buf,sizeof(buf),"fs.remove: %s",strerror(errno)); k_throw(k_err(k_str(buf)));
    }
    return k_ok(k_null());
}
static KVal* b_fs_mkdir(int argc, KVal** argv) {
    if (argc < 1 || argv[0]->kind != K_STRING) k_throw(k_err(k_str("fs.mkdir(path)")));
    if (mkdir(argv[0]->as.string_v, 0755) != 0 && errno != EEXIST) {
        char buf[256]; snprintf(buf,sizeof(buf),"fs.mkdir: %s",strerror(errno)); k_throw(k_err(k_str(buf)));
    }
    return k_ok(k_null());
}
static KVal* b_fs_read_dir(int argc, KVal** argv) {
    if (argc < 1 || argv[0]->kind != K_STRING) k_throw(k_err(k_str("fs.read_dir(path)")));
    DIR* d = opendir(argv[0]->as.string_v);
    if (!d) k_throw(k_err(k_str("fs.read_dir: open failed")));
    KVal* out = k_list_empty();
    struct dirent* de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name,".")==0 || strcmp(de->d_name,"..")==0) continue;
        k_list_push(out, k_str(de->d_name));
    }
    closedir(d);
    return out;
}

// ── json module ──────────────────────────────────────────────────────
static const char* j_skip_ws(const char* s) { while (*s && (*s==' '||*s=='\t'||*s=='\n'||*s=='\r')) s++; return s; }
static KVal* j_parse_value(const char** sp);
static KVal* j_parse_string(const char** sp) {
    const char* s = *sp;
    if (*s != '"') { k_throw(k_err(k_str("json: expected \""))); return k_null(); }
    s++;
    char* buf = (char*)malloc(4096); int n=0; int cap=4096;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) {
            s++;
            char c;
            switch(*s){
                case '"': c='"'; break; case '\\': c='\\'; break; case '/': c='/'; break;
                case 'n': c='\n'; break; case 't': c='\t'; break; case 'r': c='\r'; break;
                case 'b': c='\b'; break; case 'f': c='\f'; break;
                default: c=*s; break;
            }
            if (n+1>=cap){cap*=2;buf=(char*)realloc(buf,cap);}
            buf[n++]=c; s++;
        } else {
            if (n+1>=cap){cap*=2;buf=(char*)realloc(buf,cap);}
            buf[n++]=*s; s++;
        }
    }
    if (*s == '"') s++;
    buf[n]=0; *sp = s;
    KVal* r = k_str(buf); free(buf);
    return r;
}
static KVal* j_parse_number(const char** sp) {
    const char* s = *sp; const char* start = s;
    int is_float = 0;
    if (*s == '-') s++;
    while (*s >= '0' && *s <= '9') s++;
    if (*s == '.') { is_float=1; s++; while (*s >= '0' && *s <= '9') s++; }
    if (*s=='e'||*s=='E') { is_float=1; s++; if(*s=='+'||*s=='-')s++; while (*s>='0'&&*s<='9')s++; }
    char buf[64]; int n = (int)(s-start); if (n>63) n=63;
    memcpy(buf, start, n); buf[n]=0;
    *sp = s;
    if (is_float) return k_float(strtod(buf, NULL));
    return k_int(strtoll(buf, NULL, 10));
}
static KVal* j_parse_array(const char** sp);
static KVal* j_parse_object(const char** sp);
static KVal* j_parse_array(const char** sp) {
    const char* s = *sp; s++;
    KVal* out = k_list_empty();
    s = j_skip_ws(s);
    if (*s == ']') { s++; *sp=s; return out; }
    while (1) {
        KVal* v = j_parse_value(&s);
        k_list_push(out, v); k_release(v);
        s = j_skip_ws(s);
        if (*s == ',') { s++; s = j_skip_ws(s); continue; }
        if (*s == ']') { s++; break; }
        k_throw(k_err(k_str("json: expected , or ]")));
    }
    *sp = s;
    return out;
}
static KVal* j_parse_object(const char** sp) {
    const char* s = *sp; s++;
    KVal* out = k_map_empty();
    s = j_skip_ws(s);
    if (*s == '}') { s++; *sp=s; return out; }
    while (1) {
        s = j_skip_ws(s);
        if (*s != '"') k_throw(k_err(k_str("json: expected key")));
        KVal* key = j_parse_string(&s);
        s = j_skip_ws(s);
        if (*s != ':') k_throw(k_err(k_str("json: expected :")));
        s++; s = j_skip_ws(s);
        KVal* val = j_parse_value(&s);
        k_index_set(out, key, val);
        k_release(key); k_release(val);
        s = j_skip_ws(s);
        if (*s == ',') { s++; continue; }
        if (*s == '}') { s++; break; }
        k_throw(k_err(k_str("json: expected , or }")));
    }
    *sp = s;
    return out;
}
static KVal* j_parse_value(const char** sp) {
    const char* s = j_skip_ws(*sp);
    if (*s == '"') { KVal* r = j_parse_string(&s); *sp = s; return r; }
    if (*s == '{') { KVal* r = j_parse_object(&s); *sp = s; return r; }
    if (*s == '[') { KVal* r = j_parse_array(&s); *sp = s; return r; }
    if (strncmp(s,"true",4)==0) { *sp = s+4; return k_bool(1); }
    if (strncmp(s,"false",5)==0) { *sp = s+5; return k_bool(0); }
    if (strncmp(s,"null",4)==0) { *sp = s+4; return k_null(); }
    if ((*s=='-')||(*s>='0'&&*s<='9')) { KVal* r = j_parse_number(&s); *sp = s; return r; }
    k_throw(k_err(k_str("json: unexpected char")));
    return k_null();
}
static KVal* b_json_parse(int argc, KVal** argv) {
    if (argc < 1 || argv[0]->kind != K_STRING) k_throw(k_err(k_str("json.parse(str)")));
    const char* s = argv[0]->as.string_v;
    return j_parse_value(&s);
}

typedef struct { char* buf; int cap; int len; } JB;
static void jb_grow(JB* b, int need) {
    while (b->len + need + 1 > b->cap) b->cap = b->cap?b->cap*2:256;
    b->buf = (char*)realloc(b->buf, b->cap);
}
static void jb_puts(JB* b, const char* s) { int n=(int)strlen(s); jb_grow(b,n); memcpy(b->buf+b->len,s,n); b->len+=n; b->buf[b->len]=0; }
static void jb_putc(JB* b, char c) { jb_grow(b,1); b->buf[b->len++]=c; b->buf[b->len]=0; }
static void j_escape(JB* b, const char* s) {
    jb_putc(b,'"');
    for (; *s; s++) {
        if (*s=='"'||*s=='\\'){jb_putc(b,'\\');jb_putc(b,*s);}
        else if(*s=='\n'){jb_puts(b,"\\n");}
        else if(*s=='\t'){jb_puts(b,"\\t");}
        else if(*s=='\r'){jb_puts(b,"\\r");}
        else if((unsigned char)*s<0x20){char h[8];snprintf(h,sizeof(h),"\\u%04x",(unsigned char)*s);jb_puts(b,h);}
        else jb_putc(b,*s);
    }
    jb_putc(b,'"');
}
static void j_stringify(JB* b, KVal* v);
static void j_stringify(JB* b, KVal* v) {
    switch(v->kind) {
        case K_NULL: jb_puts(b,"null"); return;
        case K_BOOL: jb_puts(b,v->as.bool_v?"true":"false"); return;
        case K_INT: { char bb[32]; snprintf(bb,sizeof(bb),"%lld",(long long)v->as.int_v); jb_puts(b,bb); return; }
        case K_FLOAT: { char bb[64]; snprintf(bb,sizeof(bb),"%g",v->as.float_v); jb_puts(b,bb); return; }
        case K_STRING: j_escape(b,v->as.string_v); return;
        case K_LIST: case K_TUPLE: {
            jb_putc(b,'[');
            int cnt = k_len(v);
            KList* l = (v->kind==K_LIST)?v->as.list:v->as.tuple;
            for(int i=0;i<cnt;i++){if(i)jb_putc(b,',');j_stringify(b,l->items[i]);}
            jb_putc(b,']');
            return;
        }
        case K_MAP: {
            jb_putc(b,'{');
            KMap* m = v->as.map;
            int first=1;
            for(int i=0;i<m->cap;i++){
                for(KMapEntry*e=m->buckets[i];e;e=e->next){
                    if(!first)jb_putc(b,',');first=0;
                    if(e->key->kind==K_STRING)j_escape(b,e->key->as.string_v);
                    else j_stringify(b,e->key);
                    jb_putc(b,':');
                    j_stringify(b,e->val);
                }
            }
            jb_putc(b,'}');
            return;
        }
        default: { KVal* s = k_to_str(v); j_escape(b, s->as.string_v); k_release(s); return; }
    }
}
static KVal* b_json_stringify(int argc, KVal** argv) {
    if (argc<1) k_throw(k_err(k_str("json.stringify(v)")));
    JB b = {0}; j_stringify(&b, argv[0]);
    KVal* r = k_str(b.buf?b.buf:"");
    free(b.buf);
    return r;
}

// ── Channels (bounded FIFO) and threads ─────────────────────────────
#define MAX_CHANS 256
typedef struct Chan {
    KVal** buf; int cap; int head; int tail; int count; int closed;
    mtx_t m; cnd_t put_cv; cnd_t take_cv;
} Chan;
static Chan* chan_registry[MAX_CHANS]; static int chan_reg_n=0;

static KVal* b_chan_new(int argc, KVal** argv) {
    int cap = 1;
    if(argc>=1 && argv[0]->kind==K_INT) { cap=(int)argv[0]->as.int_v; if(cap<1)cap=1; }
    if (chan_reg_n >= MAX_CHANS) k_throw(k_err(k_str("too many channels")));
    Chan* c = (Chan*)calloc(1,sizeof(Chan));
    c->cap = cap;
    c->buf = (KVal**)calloc(cap,sizeof(KVal*));
    mtx_init(&c->m, mtx_plain);
    cnd_init(&c->put_cv); cnd_init(&c->take_cv);
    int id = chan_reg_n; chan_registry[id]=c; chan_reg_n++;
    return k_int(id);
}
static KVal* b_chan_send(int argc, KVal** argv) {
    if (argc<2 || argv[0]->kind!=K_INT) k_throw(k_err(k_str("chan.send(ch,v)")));
    int cid=(int)argv[0]->as.int_v; if(cid<0||cid>=chan_reg_n) k_throw(k_err(k_str("bad channel")));
    Chan* c = chan_registry[cid];
    mtx_lock(&c->m);
    while(c->count >= c->cap && !c->closed) cnd_wait(&c->put_cv, &c->m);
    if(c->closed){mtx_unlock(&c->m); k_throw(k_err(k_str("send on closed chan")));}
    c->buf[c->tail] = k_retain(argv[1]);
    c->tail=(c->tail+1)%c->cap; c->count++;
    cnd_signal(&c->take_cv);
    mtx_unlock(&c->m);
    return k_null();
}
static KVal* b_chan_recv(int argc, KVal** argv) {
    if (argc<1 || argv[0]->kind!=K_INT) k_throw(k_err(k_str("chan.recv(ch)")));
    Chan* c = chan_registry[(int)argv[0]->as.int_v]; if(!c) k_throw(k_err(k_str("bad channel")));
    mtx_lock(&c->m);
    while(c->count == 0 && !c->closed) cnd_wait(&c->take_cv, &c->m);
    if(c->count == 0 && c->closed){mtx_unlock(&c->m); return k_err(k_str("closed"));}
    KVal* r = c->buf[c->head]; c->buf[c->head]=NULL;
    c->head=(c->head+1)%c->cap; c->count--;
    cnd_signal(&c->put_cv);
    mtx_unlock(&c->m);
    return r;
}
static KVal* b_chan_close(int argc, KVal** argv) {
    if (argc<1 || argv[0]->kind!=K_INT) k_throw(k_err(k_str("chan.close(ch)")));
    Chan* c = chan_registry[(int)argv[0]->as.int_v]; if(!c) return k_null();
    mtx_lock(&c->m); c->closed=1; cnd_broadcast(&c->take_cv); cnd_broadcast(&c->put_cv); mtx_unlock(&c->m);
    return k_null();
}

static int thread_trampoline(void* arg) {
    KVal* fn = (KVal*)arg;
    KFrame fr; memset(&fr,0,sizeof(fr));
    fr.prev = k_current_frame; fr.error=NULL; k_current_frame = &fr;
    KVal* r = k_call(fn, 0, NULL);
    if (r) k_release(r);
    k_release(fn);
    k_current_frame = fr.prev;
    return 0;
}
static KVal* b_spawn(int argc, KVal** argv) {
    if (argc<1) k_throw(k_err(k_str("spawn(fn)")));
    KVal* fn = k_retain(argv[0]);
    thrd_t t;
    if (thrd_create(&t, thread_trampoline, fn) != thrd_success) {
        k_release(fn); k_throw(k_err(k_str("spawn failed")));
    }
    thrd_detach(t);
    return k_null();
}
static KVal* b_sleep(int argc, KVal** argv) {
    if (argc<1) k_throw(k_err(k_str("sleep(seconds)")));
    double s;
    if (argv[0]->kind==K_FLOAT) s=argv[0]->as.float_v;
    else if (argv[0]->kind==K_INT) s=(double)argv[0]->as.int_v;
    else k_throw(k_err(k_str("sleep: number expected")));
    struct timespec ts;
    if (s<0)s=0;
    ts.tv_sec=(time_t)s;
    ts.tv_nsec=(long)((s-ts.tv_sec)*1e9);
    thrd_sleep(&ts, NULL);
    return k_null();
}

// ── Registration ────────────────────────────────────────────────────
static KNative_* mk_native(KNativeFn fn, const char* nm) {
    KNative_* n = (KNative_*)k_alloc(sizeof(KNative_));
    n->fn = fn; n->name = nm;
    return n;
}
static KVal* mk_nv(KNativeFn fn, const char* nm) {
    KVal* v = k_new(K_NATIVE); v->as.native = mk_native(fn, nm); return v;
}
void k_register_stdlib_extra(void) {
    {
        KVal* m = k_module("fs");
        k_mod_set(m,"read_file",mk_nv(b_fs_read,"fs.read_file"));
        k_mod_set(m,"write_file",mk_nv(b_fs_write,"fs.write_file"));
        k_mod_set(m,"append_file",mk_nv(b_fs_append,"fs.append_file"));
        k_mod_set(m,"exists",mk_nv(b_fs_exists,"fs.exists"));
        k_mod_set(m,"remove",mk_nv(b_fs_remove,"fs.remove"));
        k_mod_set(m,"mkdir",mk_nv(b_fs_mkdir,"fs.mkdir"));
        k_mod_set(m,"read_dir",mk_nv(b_fs_read_dir,"fs.read_dir"));
        k_global_set("fs", m); k_release(m);
    }
    {
        KVal* m = k_module("json");
        k_mod_set(m,"parse",mk_nv(b_json_parse,"json.parse"));
        k_mod_set(m,"stringify",mk_nv(b_json_stringify,"json.stringify"));
        k_global_set("json", m); k_release(m);
    }
    {
        KVal* cm = k_module("chan");
        k_mod_set(cm,"new",mk_nv(b_chan_new,"chan.new"));
        k_mod_set(cm,"send",mk_nv(b_chan_send,"chan.send"));
        k_mod_set(cm,"recv",mk_nv(b_chan_recv,"chan.recv"));
        k_mod_set(cm,"close",mk_nv(b_chan_close,"chan.close"));
        k_global_set("chan", cm); k_release(cm);

        KVal* th = k_module("thread");
        k_mod_set(th,"spawn",mk_nv(b_spawn,"thread.spawn"));
        k_mod_set(th,"sleep",mk_nv(b_sleep,"thread.sleep"));
        k_global_set("thread", th); k_release(th);

        k_global_set("spawn", mk_nv(b_spawn,"spawn"));
        k_global_set("sleep", mk_nv(b_sleep,"sleep"));
    }
}
