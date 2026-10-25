/// codegen_krt.c - Full Kodas -> C (KRT runtime) code generator (v4, correct).
#include "codegen/codegen_c.h"
#include "../common.h"
#include "../ast.h"
#include "../lexer.h"
#include "../parser.h"
#include "../fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <alloca.h>

// ── Name generation (large rotating buffers so no alias) ──────────────
#define NB 512
static char t_bufs[NB][32];
static char a_bufs[NB][32];
static char m_bufs[NB][256];
static int t_i = 0, a_i = 0, m_i = 0;
static char* new_t(void) { char* b = t_bufs[t_i & (NB-1)]; snprintf(b, 32, "_t%d", t_i); t_i++; return b; }
static char* new_a(void) { char* b = a_bufs[a_i & (NB-1)]; snprintf(b, 32, "_a%d", a_i); a_i++; return b; }
static char* mangle(const char* n) {
    char* b = m_bufs[m_i & (NB-1)]; m_i++;
    int j = 0;
    for (const char* p = n; *p && j < 250; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '_') b[j++] = (char)c; else b[j++] = '_';
    }
    b[j] = 0; return b;
}
static char* mangle_local(const char* n) {
    char* b = m_bufs[m_i & (NB-1)]; m_i++;
    int j = 0; b[j++] = 'L'; b[j++] = '_';
    for (const char* p = n; *p && j < 250; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '_') b[j++] = (char)c; else b[j++] = '_';
    }
    b[j] = 0; return b;
}

// ── CG state ──────────────────────────────────────────────────────────
typedef struct CG CG;

#define MAX_LOCALS 512
typedef struct { const char* kodas; const char* c_name; } Local;
typedef struct Scope {
    Local locals[MAX_LOCALS]; int count; struct Scope* parent;
} Scope;
struct CG {
    FILE* out; int indent; int tmp; int had_error; char error_msg[256];
    Program* prog; Scope* scope;
};

static void cg_i(CG* g){for(int i=0;i<g->indent;i++)fputc(' ',g->out);}
static void cg_p(CG* g, const char* fmt, ...){
    va_list ap;va_start(ap,fmt);cg_i(g);vfprintf(g->out,fmt,ap);fputc('\n',g->out);va_end(ap);
}
static void cg_raw(CG* g, const char* fmt, ...){
    va_list ap;va_start(ap,fmt);vfprintf(g->out,fmt,ap);va_end(ap);
}

static Scope* scope_push(CG* g){Scope*s=(Scope*)calloc(1,sizeof(Scope));s->parent=g->scope;g->scope=s;return s;}
static void scope_pop(CG* g){Scope*s=g->scope;g->scope=s->parent;free(s);}
static void scope_add(CG*g,const char*k,const char*c){Scope*s=g->scope;s->locals[s->count].kodas=k;s->locals[s->count].c_name=c;s->count++;}
static const char* scope_lookup(CG*g,const char*n){for(Scope*s=g->scope;s;s=s->parent)for(int i=0;i<s->count;i++)if(strcmp(s->locals[i].kodas,n)==0)return s->locals[i].c_name;return NULL;}

static char* fresh_t(CG*g){char*b=new_t();cg_p(g,"KVal* %s = NULL;",b);return b;}
static void rel(CG*g,const char*t){cg_p(g,"k_release(%s);",t);}
static void cg_err(CG*g,const char*fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(g->error_msg,sizeof(g->error_msg),fmt,ap);va_end(ap);g->had_error=1;}

static void cg_string_lit(CG*g,const char*s,FILE*out){
    fputc('"',out);
    for(const char*p=s;*p;p++){unsigned char c=(unsigned char)*p;
        switch(c){case'"':fputs("\\\"",out);break;case'\\':fputs("\\\\",out);break;
        case'\n':fputs("\\n",out);break;case'\r':fputs("\\r",out);break;case'\t':fputs("\\t",out);break;
        default:if(c<32||c>=127)fprintf(out,"\\x%02x",c);else fputc(c,out);}
    }fputc('"',out);
}

static void cg_stmt(CG*g,Stmt*s);
static void cg_expr(CG*g,Expr*e,const char*T);
static char* etmp(CG*g,Expr*e){char*t=new_t();cg_p(g,"KVal* %s = NULL;",t);cg_expr(g,e,t);return t;}

// ── Expressions ──────────────────────────────────────────────────────
static void cg_expr(CG*g,Expr*e,const char*T){
    if(!e){cg_p(g,"%s = k_null();",T);return;}
    switch(e->kind){
        case EXPR_INT:cg_p(g,"%s=k_int(%lldLL);",T,(long long)e->as.int_val);return;
        case EXPR_FLOAT:cg_p(g,"%s=k_float(%.17g);",T,e->as.float_val);return;
        case EXPR_BOOL:cg_p(g,"%s=k_bool(%s);",T,e->as.bool_val?"true":"false");return;
        case EXPR_STRING:{cg_i(g);cg_raw(g,"%s=k_str(",T);cg_string_lit(g,e->as.str_val,g->out);cg_raw(g,");\n");return;}
        case EXPR_CHAR:cg_p(g,"%s=k_char('%c');",T,e->as.char_val);return;
        case EXPR_EMPTY:cg_p(g,"%s=k_empty();",T);return;
        case EXPR_IDENT:{const char*loc=scope_lookup(g,e->as.str_val);
            if(loc)cg_p(g,"%s=k_retain(%s);",T,loc);
            else{cg_i(g);cg_raw(g,"%s=k_retain(k_global_get(\"%s\"));\n",T,e->as.str_val);}return;}
        case EXPR_SELF:{const char*s=scope_lookup(g,"self");cg_p(g,"%s=k_retain(%s);",T,s?s:"_self");return;}
        case EXPR_BINARY: {
            const char* op = e->as.binary.op;
            const char* fn = "k_add_str";
            if      (!strcmp(op,"+"))  fn = "k_add_str";
            else if (!strcmp(op,"-"))  fn = "k_sub";
            else if (!strcmp(op,"*"))  fn = "k_mul";
            else if (!strcmp(op,"/"))  fn = "k_div";
            else if (!strcmp(op,"%"))  fn = "k_mod";
            else if (!strcmp(op,"==")) fn = "k_eq";
            else if (!strcmp(op,"!=")) fn = "k_neq";
            else if (!strcmp(op,"<"))  fn = "k_lt";
            else if (!strcmp(op,">"))  fn = "k_gt";
            else if (!strcmp(op,"<=")) fn = "k_lte";
            else if (!strcmp(op,">=")) fn = "k_gte";
            else if (!strcmp(op,"&&")) fn = "k_and";
            else if (!strcmp(op,"||")) fn = "k_or";
            char* a=etmp(g,e->as.binary.left);char*b=etmp(g,e->as.binary.right);
            cg_p(g,"%s=%s(%s,%s);",T,fn,a,b);rel(g,a);rel(g,b);return;
        }
        case EXPR_UNARY:{char*a=etmp(g,e->as.unary.operand);const char*op=e->as.unary.op;
            if(!strcmp(op,"-"))cg_p(g,"%s=k_neg(%s);",T,a);
            else if(!strcmp(op,"!"))cg_p(g,"%s=k_not(%s);",T,a);
            else{cg_err(g,"unary %s",op);cg_p(g,"%s=k_null();",T);}
            rel(g,a);return;}
        case EXPR_CALL:{char*callee=etmp(g,e->as.call.callee);int n=e->as.call.args.count;char**av=(char**)alloca(sizeof(char*)*n);
            for(int i=0;i<n;i++)av[i]=etmp(g,e->as.call.args.items[i]);char*arr=new_a();
            cg_p(g,"KVal** %s=(KVal**)alloca(sizeof(KVal*)*%d);",arr,n);
            for(int i=0;i<n;i++)cg_p(g,"%s[%d]=%s;",arr,i,av[i]);
            cg_p(g,"%s=k_call(%s,%d,%s);",T,callee,n,arr);
            for(int i=0;i<n;i++)rel(g,av[i]);rel(g,callee);return;}
        case EXPR_METHOD_CALL:{char*obj=etmp(g,e->as.method_call.object);int n=e->as.method_call.args.count;char**av=(char**)alloca(sizeof(char*)*n);
            for(int i=0;i<n;i++)av[i]=etmp(g,e->as.method_call.args.items[i]);char*arr=new_a();
            cg_p(g,"KVal** %s=(KVal**)alloca(sizeof(KVal*)*%d);",arr,n);
            for(int i=0;i<n;i++)cg_p(g,"%s[%d]=%s;",arr,i,av[i]);
            cg_i(g);cg_raw(g,"%s=k_method(%s,",T,obj);cg_string_lit(g,e->as.method_call.method,g->out);cg_raw(g,",%d,%s);\n",n,arr);
            for(int i=0;i<n;i++)rel(g,av[i]);rel(g,obj);return;}
        case EXPR_FIELD:{char*o=etmp(g,e->as.field.object);cg_i(g);cg_raw(g,"%s=k_get_field(%s,",T,o);cg_string_lit(g,e->as.field.field,g->out);cg_raw(g,");\n");rel(g,o);return;}
        case EXPR_INDEX:{char*c=etmp(g,e->as.index.collection);char*k=etmp(g,e->as.index.index);cg_p(g,"%s=k_index(%s,%s);",T,c,k);rel(g,c);rel(g,k);return;}
        case EXPR_LIST:{cg_p(g,"%s=k_list_empty();",T);for(int i=0;i<e->as.call.args.count;i++){char*v=etmp(g,e->as.call.args.items[i]);cg_p(g,"k_list_push(%s,%s);",T,v);rel(g,v);}return;}
        case EXPR_TUPLE:{int n=e->as.call.args.count;char**av=(char**)alloca(sizeof(char*)*n);for(int i=0;i<n;i++)av[i]=etmp(g,e->as.call.args.items[i]);
            cg_i(g);cg_raw(g,"%s=k_tuple(%d",T,n);for(int i=0;i<n;i++)cg_raw(g,",%s",av[i]);cg_raw(g,");\n");
            for(int i=0;i<n;i++)rel(g,av[i]);return;}
        case EXPR_MAP:{cg_p(g,"%s=k_map_empty();",T);int n=e->as.call.args.count;
            for(int i=0;i+1<n;i+=2){char*k=etmp(g,e->as.call.args.items[i]);char*v=etmp(g,e->as.call.args.items[i+1]);
                cg_p(g,"k_index_set(%s,%s,%s);",T,k,v);rel(g,k);rel(g,v);}return;}
        case EXPR_OK:{if(e->as.result_e.value){char*v=etmp(g,e->as.result_e.value);cg_p(g,"%s=k_ok(%s);",T,v);rel(g,v);}else cg_p(g,"%s=k_ok(k_bool(true));",T);return;}
        case EXPR_ERR:{if(e->as.result_e.value){char*v=etmp(g,e->as.result_e.value);cg_p(g,"%s=k_err(%s);",T,v);rel(g,v);}else cg_p(g,"%s=k_err(k_str(\"error\"));",T);return;}
        case EXPR_PIPE:case EXPR_ERR_PIPE:{bool iserr=e->kind==EXPR_ERR_PIPE;char*acc=etmp(g,e->as.pipe.callee);
            for(int pi=0;pi<e->as.pipe.pipe_count;pi++){char*fnv=etmp(g,e->as.pipe.pipelines[pi]);char*nxt=fresh_t(g);
                if(iserr)cg_p(g,"%s=k_pipe(%s,%s);",nxt,acc,fnv);
                else{char*arr=new_a();cg_p(g,"KVal** %s=(KVal**)alloca(sizeof(KVal*)*1);",arr);cg_p(g,"%s[0]=%s;",arr,acc);cg_p(g,"%s=k_call(%s,1,%s);",nxt,fnv,arr);}
                rel(g,acc);rel(g,fnv);acc=nxt;}
            cg_p(g,"%s=%s;",T,acc);rel(g,acc);return;}
        case EXPR_IF:{char*cv=etmp(g,e->as.if_e.cond);cg_p(g,"if(k_truthy(%s)){",cv);g->indent+=2;cg_expr(g,e->as.if_e.then_branch,T);g->indent-=2;cg_p(g,"}else{");g->indent+=2;
            if(e->as.if_e.else_branch)cg_expr(g,e->as.if_e.else_branch,T);else cg_p(g,"%s=k_null();",T);g->indent-=2;cg_p(g,"}");rel(g,cv);return;}
        case EXPR_BLOCK:{scope_push(g);for(int i=0;i<e->as.block.stmts.count;i++)cg_stmt(g,e->as.block.stmts.items[i]);cg_p(g,"%s=k_null();",T);scope_pop(g);return;}
        case EXPR_SHOW:{char*v=etmp(g,e->as.expr);cg_p(g,"k_print(%s);",v);rel(g,v);cg_p(g,"%s=k_null();",T);return;}
        case EXPR_ASSERT:{char*v=etmp(g,e->as.expr);cg_p(g,"k_assert(%s,k_str(\"assertion failed\"));",v);rel(g,v);cg_p(g,"%s=k_bool(true);",T);return;}
        case EXPR_GIVE:{if(e->as.expr){char*v=etmp(g,e->as.expr);cg_p(g,"return %s;",v);}else cg_p(g,"return k_null();");cg_p(g,"%s=k_null();",T);return;}
        case EXPR_STOP:cg_p(g,"break;");cg_p(g,"%s=k_null();",T);return;
        case EXPR_SKIP:cg_p(g,"continue;");cg_p(g,"%s=k_null();",T);return;
        case EXPR_WHILE:{cg_p(g,"while(1){");g->indent+=2;char*cv=etmp(g,e->as.loop_in.cond);cg_p(g,"if(!k_truthy(%s)){k_release(%s);break;}",cv,cv);if(e->as.loop_in.body){char*b=etmp(g,e->as.loop_in.body);rel(g,b);}g->indent-=2;cg_p(g,"}");cg_p(g,"%s=k_null();",T);return;}
        case EXPR_LOOP:{cg_p(g,"while(1){");g->indent+=2;if(e->as.loop_e.body){char*b=etmp(g,e->as.loop_e.body);rel(g,b);}g->indent-=2;cg_p(g,"}");cg_p(g,"%s=k_null();",T);return;}
        case EXPR_ASSIGN:{const char*loc=scope_lookup(g,e->as.assign.name);char*v=etmp(g,e->as.assign.value);
            if(loc){cg_p(g,"{KVal*_ov=%s;%s=k_retain(%s);k_release(_ov);}",loc,loc,v);cg_p(g,"%s=k_retain(%s);",T,loc);}
            else{cg_i(g);cg_raw(g,"k_global_set(\"%s\",k_retain(%s));\n",e->as.assign.name,v);cg_p(g,"%s=k_retain(%s);",T,v);}
            rel(g,v);return;}
        case EXPR_COMPOUND_ASSIGN:{char*rhs=etmp(g,e->as.compound.value);const char*op=e->as.compound.op;const char*fn="k_add_str";
            if(!strcmp(op,"+="))fn="k_add_str";else if(!strcmp(op,"-="))fn="k_sub";else if(!strcmp(op,"*="))fn="k_mul";else if(!strcmp(op,"/="))fn="k_div";else if(!strcmp(op,"%="))fn="k_mod";
            Expr*tgt=e->as.compound.target;
            if(tgt->kind==EXPR_IDENT){const char*lv=scope_lookup(g,tgt->as.str_val);if(lv){char*nv=fresh_t(g);cg_p(g,"%s=%s(%s,%s);",nv,fn,lv,rhs);cg_p(g,"{KVal*_ov=%s;%s=k_retain(%s);k_release(_ov);}",lv,lv,nv);cg_p(g,"%s=k_retain(%s);",T,lv);rel(g,nv);}else{cg_p(g,"%s=k_null();",T);}}
            else{cg_p(g,"%s=k_null();",T);}
            rel(g,rhs);return;}
        case EXPR_FIELD_ASSIGN:{char*o=etmp(g,e->as.field_assign.object);char*v=etmp(g,e->as.field_assign.value);cg_i(g);cg_raw(g,"k_set_field(%s,",o);cg_string_lit(g,e->as.field_assign.field,g->out);cg_raw(g,",%s);\n",v);cg_p(g,"%s=k_retain(%s);",T,v);rel(g,o);rel(g,v);return;}
        case EXPR_STRUCT_INIT:{int n=e->as.struct_init.field_count;char**av=(char**)alloca(sizeof(char*)*n);for(int i=0;i<n;i++)av[i]=etmp(g,e->as.struct_init.field_vals[i]);
            cg_p(g,"static const char*_sfns[]={");g->indent+=2;for(int i=0;i<n;i++){cg_i(g);cg_string_lit(g,e->as.struct_init.field_names[i],g->out);cg_raw(g,",\n");}g->indent-=2;cg_p(g,"};");
            cg_i(g);cg_raw(g,"%s=k_struct_new(\"%s\",%d,(const char**)_sfns",T,mangle(e->as.struct_init.name),n);for(int i=0;i<n;i++)cg_raw(g,",%s",av[i]);cg_raw(g,");\n");
            for(int i=0;i<n;i++)rel(g,av[i]);return;}
        case EXPR_LET:{const char*nm=e->as.let.name;const char*c=mangle_local(nm);cg_p(g,"KVal* %s=k_null();",c);scope_add(g,nm,c);
            if(e->as.let.value){char*v=etmp(g,e->as.let.value);cg_p(g,"{KVal*_ov=%s;%s=k_retain(%s);k_release(_ov);}",c,c,v);rel(g,v);}
            cg_p(g,"%s=k_null();",T);return;}
        case EXPR_SEAL:cg_p(g,"%s=k_null();",T);return;
        case EXPR_FN:case EXPR_LAMBDA:{const char*fn=e->as.fn_e.name&&*e->as.fn_e.name?e->as.fn_e.name:"_lambda";cg_i(g);cg_raw(g,"%s=k_retain(k_global_get(\"%s\"));\n",T,fn);return;}
        default:cg_err(g,"NYI expr %d",(int)e->kind);cg_p(g,"%s=k_null();",T);return;
    }
}

// ── Statements ──────────────────────────────────────────────────────
static void emit_fn_body(CG*g,ParamList*params,Expr*body){
    cg_p(g,"(void)_env;(void)_argc;");scope_push(g);int n=params->count;
    for(int i=0;i<n;i++){Param*p=&params->items[i];bool isself=p->is_self||(i==0&&strcmp(p->name,"self")==0);
        const char*c=mangle_local(p->name);cg_p(g,"KVal* %s=k_null();",c);scope_add(g,p->name,c);
        cg_p(g,"{KVal*_pv=(_argc>%d)?k_retain(_argv[%d]):k_null();",i,i);cg_p(g,"KVal*_ov=%s;%s=k_retain(_pv);k_release(_ov);k_release(_pv);}",c,c);
        if(isself)cg_p(g,"KVal* _self=%s;",c);}
    if(body){char*v=etmp(g,body);cg_p(g,"return %s;",v);}else cg_p(g,"return k_null();");scope_pop(g);
}
static void cg_stmt(CG*g,Stmt*s){
    if(!s)return;
    switch(s->kind){
        case STMT_EXPR:{char*v=etmp(g,s->as.expr);rel(g,v);return;}
        case STMT_LET:case STMT_SEAL:{const char*nm=s->as.let_s.name;const char*c=mangle_local(nm);cg_p(g,"KVal* %s=k_null();",c);scope_add(g,nm,c);
            if(s->as.let_s.value){char*v=etmp(g,s->as.let_s.value);cg_p(g,"{KVal*_ov=%s;%s=k_retain(%s);k_release(_ov);}",c,c,v);rel(g,v);}return;}
        case STMT_RETURN:{if(s->as.expr){char*v=etmp(g,s->as.expr);cg_p(g,"return %s;",v);}else cg_p(g,"return k_null();");return;}
        default:break;
    }
}

static void emit_fn_def(CG*g,const char*fn_c,ParamList*params,Expr*body){
    cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv){",fn_c);g->indent+=2;emit_fn_body(g,params,body);g->indent-=2;cg_p(g,"}");
}

static void cg_preamble(CG*g){cg_p(g,"// Auto-generated by Kodas (KRT backend) — DO NOT EDIT");cg_p(g,"#include\"krt.h\"");cg_p(g,"#include<alloca.h>");cg_p(g,"");}
static void cg_entry(CG*g){
    cg_p(g,"KVal* k_entry(int _kargc,KVal**_kargv){");g->indent+=2;cg_p(g,"(void)_kargc;(void)_kargv;");
    for(Stmt*s=g->prog->first;s;s=s->next){
        if(s->kind==STMT_FN){const char*fn=mangle(s->as.fn_s.name);cg_p(g,"k_global_set(\"%s\",k_closure(fn_%s,0,NULL,%d,\"%s\"));",s->as.fn_s.name,fn,s->as.fn_s.params.count,s->as.fn_s.name);}
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){FnDecl*m=&mt->items[i];char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(m->name));cg_p(g,"k_global_set(\"%s\",k_closure(fn_%s,0,NULL,%d,\"%s\"));",mn,mn,m->params.count,mn);}}
    }
    for(Stmt*s=g->prog->first;s;s=s->next){switch(s->kind){
        case STMT_FN:case STMT_STRUCT:case STMT_ENUM:case STMT_BEHAVE:case STMT_TRAIT:case STMT_IMPL:case STMT_TEST:case STMT_BENCH:case STMT_BRIDGE:case STMT_CONTRACT:case STMT_MODULE:case STMT_IMPORT:case STMT_DECORATOR:case STMT_PUB:continue;
        default:cg_stmt(g,s);break;}}
    cg_p(g,"KVal* _main_fn=k_global_get(\"main\");");cg_p(g,"if(_main_fn){");g->indent+=2;
    cg_p(g,"KVal*_r=k_call(_main_fn,0,NULL);if(_r)k_release(_r);");g->indent-=2;cg_p(g,"}");cg_p(g,"return k_null();");g->indent-=2;cg_p(g,"}");
}

int codegen_krt(Program*prog,const char*out_path,const char*source_path){
    (void)source_path;FILE*f=fopen(out_path,"w");if(!f){fprintf(stderr,"failed to open %s\n",out_path);return 0;}
    CG g_;memset(&g_,0,sizeof(g_));CG*g=&g_;g->out=f;g->prog=prog;cg_preamble(g);
    for(Stmt*s=prog->first;s;s=s->next){if(s->kind==STMT_FN)cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv);",mangle(s->as.fn_s.name));
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(mt->items[i].name));cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv);",mn);}}}
    cg_p(g,"");
    for(Stmt*s=prog->first;s;s=s->next){t_i=0;a_i=0;if(s->kind==STMT_FN){emit_fn_def(g,mangle(s->as.fn_s.name),&s->as.fn_s.params,s->as.fn_s.body);cg_p(g,"");}
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){
            FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(mt->items[i].name));
            cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv){",mn);g->indent+=2;emit_fn_body(g,&mt->items[i].params,mt->items[i].body);g->indent-=2;cg_p(g,"}");cg_p(g,"");}}}
    t_i=0;a_i=0;cg_entry(g);fclose(f);
    if(g->had_error){fprintf(stderr,"krt codegen error: %s\n",g->error_msg);return 0;}return 1;
}
