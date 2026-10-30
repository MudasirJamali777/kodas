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
#define MAX_LAMBDAS 256
#define MAX_CAPS 32
typedef struct { const char* kodas; const char* c_name; } Local;
typedef struct Scope {
    Local locals[MAX_LOCALS]; int count; struct Scope* parent;
} Scope;
typedef struct { ParamList params; Expr* body; int capture_count; const char* captures[MAX_CAPS]; } Lambda;
struct CG {
    FILE* out; int indent; int tmp; int had_error; char error_msg[256];
    Program* prog; Scope* scope;
    int lambda_count; int lambda_count_final; Lambda lambdas[MAX_LAMBDAS];
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
static char* fresh_lambda(CG*g){(void)g;static char lb[64];snprintf(lb,sizeof(lb),"__lambda_%d",g->lambda_count);return lb;}
static void rel(CG*g,const char*t){cg_p(g,"k_release(%s);",t);}

// Free-variable collection for closures: walk expression tree, add identifier names
// that resolve in current scope (i.e., are not locals of the lambda itself) and are not already in caps[].
static void cap_add(const char*name, const char**caps, int*n, int max){
    if(!name||!*name)return;
    if(strcmp(name,"self")==0)return; // self handled by method dispatch
    for(int i=0;i<*n;i++)if(strcmp(caps[i],name)==0)return;
    if(*n<max)caps[(*n)++]=name;
}
static bool param_has(ParamList*pl,const char*n){
    for(int i=0;i<pl->count;i++)if(strcmp(pl->items[i].name,n)==0)return true;
    return false;
}
static void collect_free_expr(CG*g, Expr*e, const char**caps, int*n, int max, ParamList*params);
static void collect_free_stmt(CG*g, Stmt*s, const char**caps, int*n, int max, ParamList*params){
    if(!s)return;
    switch(s->kind){
        case STMT_EXPR: if(s->as.expr)collect_free_expr(g,s->as.expr,caps,n,max,params);break;
        case STMT_LET:case STMT_SEAL:
            if(s->as.let_s.value)collect_free_expr(g,s->as.let_s.value,caps,n,max,params);
            break;
        case STMT_RETURN: if(s->as.expr)collect_free_expr(g,s->as.expr,caps,n,max,params);break;
        default:break;
    }
    if(s->next)collect_free_stmt(g,s->next,caps,n,max,params);
}
static void collect_free_expr(CG*g, Expr*e, const char**caps, int*n, int max, ParamList*params){
    if(!e)return;
    switch(e->kind){
        case EXPR_IDENT:{
            if(!param_has(params,e->as.str_val)){
                const char*loc=scope_lookup(g,e->as.str_val);
                if(loc)cap_add(e->as.str_val,caps,n,max);
            }
            return;
        }
        case EXPR_SELF:{const char*s=scope_lookup(g,"self");if(s)cap_add("self",caps,n,max);return;}
        case EXPR_BINARY:collect_free_expr(g,e->as.binary.left,caps,n,max,params);collect_free_expr(g,e->as.binary.right,caps,n,max,params);return;
        case EXPR_UNARY:collect_free_expr(g,e->as.unary.operand,caps,n,max,params);return;
        case EXPR_CALL:{collect_free_expr(g,e->as.call.callee,caps,n,max,params);
            for(int i=0;i<e->as.call.args.count;i++)collect_free_expr(g,e->as.call.args.items[i],caps,n,max,params);return;}
        case EXPR_METHOD_CALL:{collect_free_expr(g,e->as.method_call.object,caps,n,max,params);
            for(int i=0;i<e->as.method_call.args.count;i++)collect_free_expr(g,e->as.method_call.args.items[i],caps,n,max,params);return;}
        case EXPR_FIELD:collect_free_expr(g,e->as.field.object,caps,n,max,params);return;
        case EXPR_INDEX:collect_free_expr(g,e->as.index.collection,caps,n,max,params);collect_free_expr(g,e->as.index.index,caps,n,max,params);return;
        case EXPR_LIST:for(int i=0;i<e->as.call.args.count;i++)collect_free_expr(g,e->as.call.args.items[i],caps,n,max,params);return;
        case EXPR_TUPLE:for(int i=0;i<e->as.call.args.count;i++)collect_free_expr(g,e->as.call.args.items[i],caps,n,max,params);return;
        case EXPR_MAP:for(int i=0;i<e->as.call.args.count;i++)collect_free_expr(g,e->as.call.args.items[i],caps,n,max,params);return;
        case EXPR_SET:for(int i=0;i<e->as.call.args.count;i++)collect_free_expr(g,e->as.call.args.items[i],caps,n,max,params);return;
        case EXPR_OK:case EXPR_ERR:if(e->as.result_e.value)collect_free_expr(g,e->as.result_e.value,caps,n,max,params);return;
        case EXPR_PIPE:case EXPR_ERR_PIPE:{collect_free_expr(g,e->as.pipe.callee,caps,n,max,params);
            for(int i=0;i<e->as.pipe.pipe_count;i++)collect_free_expr(g,e->as.pipe.pipelines[i],caps,n,max,params);return;}
        case EXPR_IF:collect_free_expr(g,e->as.if_e.cond,caps,n,max,params);collect_free_expr(g,e->as.if_e.then_branch,caps,n,max,params);
            if(e->as.if_e.else_branch)collect_free_expr(g,e->as.if_e.else_branch,caps,n,max,params);return;
        case EXPR_BLOCK:for(int i=0;i<e->as.block.stmts.count;i++)collect_free_stmt(g,e->as.block.stmts.items[i],caps,n,max,params);return;
        case EXPR_SHOW:case EXPR_ASSERT:case EXPR_GIVE:collect_free_expr(g,e->as.expr,caps,n,max,params);return;
        case EXPR_WHILE:collect_free_expr(g,e->as.loop_in.cond,caps,n,max,params);collect_free_expr(g,e->as.loop_in.body,caps,n,max,params);return;
        case EXPR_LOOP:collect_free_expr(g,e->as.loop_e.body,caps,n,max,params);return;
        case EXPR_LOOP_IN:collect_free_expr(g,e->as.loop_in.iter,caps,n,max,params);collect_free_expr(g,e->as.loop_in.body,caps,n,max,params);return;
        case EXPR_ASSIGN:collect_free_expr(g,e->as.assign.value,caps,n,max,params);return;
        case EXPR_COMPOUND_ASSIGN:collect_free_expr(g,e->as.compound.value,caps,n,max,params);return;
        case EXPR_FIELD_ASSIGN:collect_free_expr(g,e->as.field_assign.object,caps,n,max,params);collect_free_expr(g,e->as.field_assign.value,caps,n,max,params);return;
        case EXPR_STRUCT_INIT:for(int i=0;i<e->as.struct_init.field_count;i++)collect_free_expr(g,e->as.struct_init.field_vals[i],caps,n,max,params);return;
        case EXPR_LET:if(e->as.let.value)collect_free_expr(g,e->as.let.value,caps,n,max,params);return;
        case EXPR_MATCH:{collect_free_expr(g,e->as.match_e.obj,caps,n,max,params);
            for(int i=0;i<e->as.match_e.arm_count;i++)collect_free_expr(g,e->as.match_e.arms[i].body,caps,n,max,params);return;}
        case EXPR_RANGE:case EXPR_RANGE_INCL:collect_free_expr(g,e->as.binary.left,caps,n,max,params);collect_free_expr(g,e->as.binary.right,caps,n,max,params);return;
        case EXPR_INTERP_STRING:for(int i=0;i<e->as.interp.count;i++)collect_free_expr(g,e->as.interp.parts[i],caps,n,max,params);return;
        case EXPR_FN:case EXPR_LAMBDA:case EXPR_LAMBDA_SHORT:return; // don't recurse into nested lambdas (their own captures handled on emit)
        default:return;
    }
}
static void collect_free(CG*g, Expr*body, const char**caps, int*n, int max){
    ParamList empty={0};
    collect_free_expr(g,body,caps,n,max,&empty);
}
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
        case EXPR_FN:
        case EXPR_LAMBDA:case EXPR_LAMBDA_SHORT:{
            if(e->kind==EXPR_FN&&e->as.fn_e.name&&e->as.fn_e.name[0]){
                // Named fn reference (inner recursive name binding).
                const char*fn=e->as.fn_e.name;
                cg_p(g,"%s=k_retain(k_global_get(\"%s\"));",T,fn);return;
            }
            // Anonymous fn/lambda: closure with capture lifting.
            int _li = g->lambda_count; g->lambda_count++;
            const char* caps[32]; int ncap=0;
            collect_free(g, e->as.fn_e.body, caps, &ncap, 32);
            char*earr=new_a();
            cg_p(g,"KVal** %s=(KVal**)alloca(sizeof(KVal*)*%d);",earr,ncap);
            for(int ci=0;ci<ncap;ci++){
                const char*cv=scope_lookup(g,caps[ci]);
                if(!cv){cv="_env_dummy";cg_p(g,"KVal* _env_dummy=k_null();");}
                cg_p(g,"%s[%d]=k_retain(%s);",earr,ci,cv);
            }
            char*lname=new_t(); t_i++;
            cg_p(g,"KVal* %s=k_closure(fn___lambda_%d,%d,%s,%d,\"__lambda_%d\");",lname,_li,ncap,earr,e->as.fn_e.params.count,_li);
            for(int ci=0;ci<ncap;ci++){cg_p(g,"k_release(%s[%d]);",earr,ci);}
            cg_p(g,"%s=k_retain(%s);",T,lname);rel(g,lname);
            g->lambdas[_li].body=e->as.fn_e.body;
            g->lambdas[_li].params=e->as.fn_e.params;
            g->lambdas[_li].capture_count=ncap;
            for(int ci=0;ci<ncap;ci++)g->lambdas[_li].captures[ci]=caps[ci];
            return;}
        case EXPR_INTERP_STRING:{
            // Interpolated string: concat the parts sequentially; each part alternates string/expr/string...
            // Parser produces interp.parts list: literal strings between ${...} and expressions inside.
            // We assume parts[i] for even i is a literal string, for odd i is an expression.
            {int n=e->as.interp.count;
            if(n==0){cg_p(g,"%s=k_str(\"\");",T);return;}
            char*acc=NULL;
            for(int i=0;i<n;i++){
                Expr*p=e->as.interp.parts[i];
                char*rhs;
                if(p->kind==EXPR_STRING){rhs=etmp(g,p);}
                else{char*v=etmp(g,p);rhs=fresh_t(g);cg_p(g,"%s=k_to_str(%s);",rhs,v);rel(g,v);}
                if(!acc){acc=rhs;}
                else{char*nxt=fresh_t(g);cg_p(g,"%s=k_add_str(%s,%s);",nxt,acc,rhs);rel(g,acc);rel(g,rhs);acc=nxt;}
            }
            cg_p(g,"%s=k_retain(%s);",T,acc);rel(g,acc);}
            return;}
        case EXPR_RANGE:case EXPR_RANGE_INCL:{
            bool incl=e->kind==EXPR_RANGE_INCL;
            // Handle integer ranges a..b and a..=b. If bounds are not int literals, evaluate them.
            char*sv=etmp(g,e->as.binary.left);char*ev=etmp(g,e->as.binary.right);
            cg_i(g);cg_raw(g,"%s=k_range(k_to_int(%s)->as.int_v,k_to_int(%s)->as.int_v,%s);\n",T,sv,ev,incl?"true":"false");
            rel(g,sv);rel(g,ev);return;}
        case EXPR_SET:{cg_p(g,"%s=k_set_empty();",T);
            for(int i=0;i<e->as.call.args.count;i++){char*v=etmp(g,e->as.call.args.items[i]);cg_p(g,"k_set_add(%s,%s);",T,v);rel(g,v);}return;}
        case EXPR_LOOP_IN:{
            // for <var> [, <idxvar>] in <iter> { body }
            // Use fresh t names but assign k_null() in-place (fresh_t already declares them).
            {char*vname=new_t();char*iname=NULL;
            cg_p(g,"KVal* %s=k_null();",vname);
            if(e->as.loop_in.idx_var){iname=new_t();cg_p(g,"KVal* %s=k_null();",iname);}
            scope_push(g);scope_add(g,e->as.loop_in.var,vname);
            if(e->as.loop_in.idx_var)scope_add(g,e->as.loop_in.idx_var,iname);
            char*col=etmp(g,e->as.loop_in.iter);
            char*it=new_t();cg_p(g,"KIter %s=k_iter_new(%s);",it,col);
            cg_p(g,"{");
            cg_p(g,"KVal*_itv=NULL;int _i=0;");
            cg_p(g,"while((_itv=k_iter_next(&%s))){",it);g->indent+=2;
            cg_p(g,"{KVal*_ov=%s;%s=_itv;k_release(_ov);}",vname,vname);
            if(iname){cg_p(g,"{KVal*_ov2=%s;%s=k_int(_i);k_release(_ov2);}",iname,iname);}
            if(e->as.loop_in.body){char*b=etmp(g,e->as.loop_in.body);rel(g,b);}
            cg_p(g,"_i++;");g->indent-=2;cg_p(g,"}");
            cg_p(g,"k_release(%s);",vname);
            if(iname){cg_p(g,"k_release(%s);",iname);}
            cg_p(g,"k_release(%s);}",col);
            scope_pop(g);
            cg_p(g,"%s=k_null();",T);
            }return;}
        case EXPR_MATCH:{
            char*mv=etmp(g,e->as.match_e.obj);
            char*done=new_t();char*result=new_t();
            cg_p(g,"int %s=0;",done);
            cg_p(g,"KVal* %s=k_null();",result);
            for(int ai=0;ai<e->as.match_e.arm_count;ai++){
                Pattern*pat=e->as.match_e.arms[ai].pat;Expr*body=e->as.match_e.arms[ai].body;
                cg_p(g,"if(!%s){",done);g->indent+=2;
                bool opens_block=true;
                if(pat->kind==PAT_WILDCARD){cg_p(g,"{");opens_block=true;}
                else if(pat->kind==PAT_BINDING){
                    const char*v=pat->name;const char*cv=mangle_local(v);
                    cg_p(g,"{KVal* %s=k_retain(%s);",cv,mv);opens_block=true;
                    // Register in scope for the body but pop after (emitted via separate block)
                    scope_add(g,v,cv);
                }
                else if(pat->kind==PAT_LITERAL){char*lv=etmp(g,pat->literal);
                    cg_p(g,"if(k_eq_deep(%s,%s)){",mv,lv);g->indent+=2;rel(g,lv);opens_block=true;}
                else if(pat->kind==PAT_ENUM_VARIANT){
                    const char*vn=pat->name;
                    cg_p(g,"if(%s->kind==K_ENUM&&strcmp(%s->as.enum_v->variant,\"%s\")==0){",mv,mv,vn);g->indent+=2;
                    for(int pi=0;pi<pat->sub_count;pi++){
                        if(pat->sub[pi]->kind==PAT_BINDING){
                            const char*v=pat->sub[pi]->name;const char*cv=mangle_local(v);
                            cg_p(g,"KVal* %s=k_retain(%s->as.enum_v->payload[%d]);",cv,mv,pi);scope_add(g,v,cv);
                        }
                    }
                    opens_block=true;
                }
                else{cg_p(g,"{/* NYI pat %d, treat as wildcard */",(int)pat->kind);opens_block=true;}
                if(pat->guard){char*gv=etmp(g,pat->guard);cg_p(g,"if(k_truthy(%s)){",gv);g->indent+=2;rel(g,gv);}
                {char*bv=etmp(g,body);
                cg_p(g,"{KVal*_ov=%s;%s=k_retain(%s);k_release(_ov);}",result,result,bv);
                cg_p(g,"%s=1;",done);rel(g,bv);}
                if(pat->guard){g->indent-=2;cg_p(g,"}");}
                if(opens_block){g->indent-=2;cg_p(g,"}");}
                g->indent-=2;cg_p(g,"}");
            }
            cg_p(g,"%s=k_retain(%s);",T,result);
            cg_p(g,"k_release(%s);k_release(%s);",mv,result);
            return;}
        case EXPR_ARRAY_COMP:{
            // [elem for x[,i] in iter if cond]
            cg_p(g,"%s=k_list_empty();",T);
            const char*vname=new_t();cg_p(g,"KVal* %s=k_null();",vname);
            const char*iname=NULL;
            if(e->as.comp.idx_var){iname=new_t();cg_p(g,"KVal* %s=k_null();",iname);}
            scope_push(g);scope_add(g,e->as.comp.var,vname);
            if(e->as.comp.idx_var)scope_add(g,e->as.comp.idx_var,iname);
            char*col=etmp(g,e->as.comp.iter);
            char*it=new_t();cg_p(g,"KIter %s=k_iter_new(%s);",it,col);
            cg_p(g,"{");g->indent+=2;
            cg_p(g,"KVal*_itv=NULL;int _i=0;");
            cg_p(g,"while((_itv=k_iter_next(&%s))){",it);g->indent+=2;
            cg_p(g,"{KVal*_ov=%s;%s=_itv;k_release(_ov);}",vname,vname);
            if(iname){cg_p(g,"{KVal*_ov2=%s;%s=k_int(_i);k_release(_ov2);}",iname,iname);}
            cg_p(g,"{");g->indent+=2;
            if(e->as.comp.cond){char*c=etmp(g,e->as.comp.cond);cg_p(g,"if(k_to_bool(%s)){",c);g->indent+=2;rel(g,c);}
            char*el=etmp(g,e->as.comp.elem);
            cg_p(g,"k_list_push(%s,%s);",T,el);rel(g,el);
            if(e->as.comp.cond){g->indent-=2;cg_p(g,"}");}
            g->indent-=2;cg_p(g,"}");
            cg_p(g,"_i++;");
            g->indent-=2;cg_p(g,"}");
            cg_p(g,"k_release(%s);",vname);
            if(iname)cg_p(g,"k_release(%s);",iname);
            cg_p(g,"k_release(%s);",col);
            g->indent-=2;cg_p(g,"}");
            scope_pop(g);
            return;}
        case EXPR_SPREAD:{
            // Spread in list literal: copy items from collection into target — for now evaluate as k_list_copy.
            if(e->as.expr){char*v=etmp(g,e->as.expr);cg_p(g,"%s=k_retain(%s);",T,v);rel(g,v);}else cg_p(g,"%s=k_null();",T);
            return;}
        case EXPR_STRUCT_LIT:{
            // Same as struct_init
            cg_p(g,"%s=k_null();",T);return;}
        case EXPR_LIST_PUSH:{
            // parser helper for <list>.push(x)
            if(e->as.list_push.elem){char*v=etmp(g,e->as.list_push.elem);cg_p(g,"%s=k_retain(%s);",T,v);rel(g,v);}else cg_p(g,"%s=k_null();",T);
            return;}
        case EXPR_THEN_ELSE:{char*cv=etmp(g,e->as.then_else.cond);char*tv=etmp(g,e->as.then_else.then_val);char*fv=etmp(g,e->as.then_else.else_val);
            cg_p(g,"%s=k_truthy(%s)?k_retain(%s):k_retain(%s);",T,cv,tv,fv);rel(g,cv);rel(g,tv);rel(g,fv);return;}
        default:cg_err(g,"NYI expr %d",(int)e->kind);cg_p(g,"%s=k_null();",T);return;
    }
}

// ── Statements ──────────────────────────────────────────────────────
static void emit_fn_body_caps(CG*g,ParamList*params,Expr*body,int ncap,const char**caps);
static void emit_fn_body(CG*g,ParamList*params,Expr*body){
    emit_fn_body_caps(g,params,body,0,NULL);
}
static void emit_fn_body_caps(CG*g,ParamList*params,Expr*body,int ncap,const char**caps){
    if(ncap==0)cg_p(g,"(void)_env;");
    cg_p(g,"(void)_argc;");scope_push(g);
    // Bind captured names to _env[i]
    for(int ci=0;ci<ncap;ci++){
        char cname[64];snprintf(cname,sizeof(cname),"L_c_%s",mangle(caps[ci]));
        cg_p(g,"KVal* %s=k_retain(_env[%d]);",cname,ci);
        scope_add(g,caps[ci],cname);
    }
    int n=params->count;
    for(int i=0;i<n;i++){Param*p=&params->items[i];bool isself=p->is_self||(i==0&&strcmp(p->name,"self")==0);
        const char*c=mangle_local(p->name);cg_p(g,"KVal* %s=k_null();",c);scope_add(g,p->name,c);
        cg_p(g,"{KVal*_pv=(_argc>%d)?k_retain(_argv[%d]):k_null();",i,i);cg_p(g,"KVal*_ov=%s;%s=k_retain(_pv);k_release(_ov);k_release(_pv);}",c,c);
        if(isself)cg_p(g,"KVal* _self=%s;",c);}
    if(body){char*v=etmp(g,body);cg_p(g,"return %s;",v);}else cg_p(g,"return k_null();");
    // Release captured vars
    for(int ci=0;ci<ncap;ci++){char cname[64];snprintf(cname,sizeof(cname),"L_c_%s",mangle(caps[ci]));cg_p(g,"k_release(%s);",cname);}
    scope_pop(g);
}
static void cg_stmt(CG*g,Stmt*s){
    if(!s)return;
    switch(s->kind){
        case STMT_EXPR:{char*v=etmp(g,s->as.expr);rel(g,v);return;}
        case STMT_LET:case STMT_SEAL:{
            if(s->as.let_s.is_destruct){
                // Tuple destructuring let (a,b,_) = expr
                char*rhs=s->as.let_s.value?etmp(g,s->as.let_s.value):"k_null()";
                // Declare all bindings first, then assign
                char**cn=(char**)alloca(sizeof(char*)*s->as.let_s.destruct_count);
                for(int i=0;i<s->as.let_s.destruct_count;i++){
                    const char*nm=s->as.let_s.names[i];
                    if(!nm){cn[i]=NULL;continue;}
                    cn[i]=mangle_local(nm);cg_p(g,"KVal* %s=k_null();",cn[i]);scope_add(g,nm,cn[i]);
                }
                // Extract fields via k_index(rhs, i)
                for(int i=0;i<s->as.let_s.destruct_count;i++){
                    if(!cn[i])continue;
                    char*ki=new_t();t_i++;cg_p(g,"KVal* %s=k_int(%d);",ki,i);
                    char*el=new_t();t_i++;cg_p(g,"KVal* %s=k_index(%s,%s);",el,rhs,ki);
                    cg_p(g,"{KVal*_ov=%s;%s=k_retain(%s);k_release(_ov);}",cn[i],cn[i],el);
                    rel(g,ki);rel(g,el);
                }
                if(s->as.let_s.value)rel(g,rhs);
                return;
            }
            const char*nm=s->as.let_s.name;const char*c=mangle_local(nm);cg_p(g,"KVal* %s=k_null();",c);scope_add(g,nm,c);
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
        if(s->kind==STMT_STRUCT){
            cg_p(g,"k_global_set(\"%s\",k_type(\"%s\"));",s->as.struct_s.name,s->as.struct_s.name);
        }
        else if(s->kind==STMT_ENUM){
            cg_p(g,"k_global_set(\"%s\",k_type(\"%s\"));",s->as.enum_s.name,s->as.enum_s.name);
        }
        else if(s->kind==STMT_FN){const char*fn=mangle(s->as.fn_s.name);cg_p(g,"k_global_set(\"%s\",k_closure(fn_%s,0,NULL,%d,\"%s\"));",s->as.fn_s.name,fn,s->as.fn_s.params.count,s->as.fn_s.name);}
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){FnDecl*m=&mt->items[i];char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(m->name));
            bool is_static=true;for(int p=0;p<m->params.count;p++)if(m->params.items[p].is_self){is_static=false;break;}
            cg_p(g,"k_global_set(\"%s\",k_closure(fn_%s,0,NULL,%d,\"%s\"));",mn,mn,m->params.count-(is_static?0:1),mn);}}
    }
    // Register lambdas as globals (zero-env closures for now; captures to be added)
    for(int i=0;i<g->lambda_count_final;i++){cg_p(g,"k_global_set(\"__lambda_%d\",k_closure(fn___lambda_%d,0,NULL,0,\"__lambda_%d\"));",i,i,i);}
    // Register enum variant constructors
    for(Stmt*s=g->prog->first;s;s=s->next){if(s->kind==STMT_ENUM){
        for(int vi=0;vi<s->as.enum_s.variants.count;vi++){
            EnumVariant*v=&s->as.enum_s.variants.items[vi];
            char fnn[128];snprintf(fnn,sizeof(fnn),"k_ev_%s",mangle(v->name));
            cg_p(g,"k_global_set(\"%s\",k_native(%s,\"%s\"));",v->name,fnn,v->name);
        }
    }}
    // Emit test blocks (each runs after main in k_entry)
    for(Stmt*s=g->prog->first;s;s=s->next){if(s->kind==STMT_TEST){
        const char*nm=s->as.test_s.name?:"<test>";
        cg_i(g); fprintf(g->out,"fprintf(stderr,\"test %%s: \",");
        cg_raw(g,"\"%s\");\n",nm);
        if(s->as.test_s.body){char*v=etmp(g,s->as.test_s.body);cg_p(g,"k_release(%s);",v);}
        cg_p(g,"fprintf(stderr,\"ok\\n\");");
    }}
    for(Stmt*s=g->prog->first;s;s=s->next){switch(s->kind){
        case STMT_FN:case STMT_STRUCT:case STMT_ENUM:case STMT_BEHAVE:case STMT_TRAIT:case STMT_IMPL:case STMT_TEST:case STMT_BENCH:case STMT_BRIDGE:case STMT_CONTRACT:case STMT_MODULE:case STMT_IMPORT:case STMT_DECORATOR:case STMT_PUB:continue;
        default:cg_stmt(g,s);break;}}
    cg_p(g,"KVal* _main_fn=k_global_get(\"main\");");cg_p(g,"if(_main_fn){");g->indent+=2;
    cg_p(g,"KVal*_r=k_call(_main_fn,0,NULL);if(_r)k_release(_r);");g->indent-=2;cg_p(g,"}");cg_p(g,"return k_null();");g->indent-=2;cg_p(g,"}");
}

int codegen_krt(Program*prog,const char*out_path,const char*source_path){
    (void)source_path;FILE*f=fopen(out_path,"w");if(!f){fprintf(stderr,"failed to open %s\n",out_path);return 0;}
    CG g_;memset(&g_,0,sizeof(g_));CG*g=&g_;g->out=f;g->prog=prog;
    // First pass: count lambdas (run codegen into /dev/null to discover lambda_count).
    FILE*nul=fopen("/dev/null","w");g->out=nul;g->lambda_count=0;
    // Emit fn decls + bodies just to count lambdas, but don't produce final output.
    cg_preamble(g);
    for(Stmt*s=prog->first;s;s=s->next){t_i=0;a_i=0;
        if(s->kind==STMT_FN){emit_fn_def(g,mangle(s->as.fn_s.name),&s->as.fn_s.params,s->as.fn_s.body);cg_p(g,"");}
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){
            FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(mt->items[i].name));
            cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv){",mn);g->indent+=2;emit_fn_body(g,&mt->items[i].params,mt->items[i].body);g->indent-=2;cg_p(g,"}");cg_p(g,"");}}}
    int nlambdas=g->lambda_count;
    fclose(nul);g->indent=0;

    // Second pass: real output.
    g->out=f;g->lambda_count=0;g->scope=NULL;
    cg_preamble(g);
    for(Stmt*s=prog->first;s;s=s->next){if(s->kind==STMT_FN)cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv);",mangle(s->as.fn_s.name));
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(mt->items[i].name));cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv);",mn);}}}
    for(int i=0;i<nlambdas;i++)cg_p(g,"static KVal* fn___lambda_%d(KVal**_env,int _argc,KVal**_argv);",i);
    cg_p(g,"");
    for(Stmt*s=prog->first;s;s=s->next){t_i=0;a_i=0;if(s->kind==STMT_FN){emit_fn_def(g,mangle(s->as.fn_s.name),&s->as.fn_s.params,s->as.fn_s.body);cg_p(g,"");}
        else if(s->kind==STMT_BEHAVE||s->kind==STMT_IMPL){
            FnDeclList*mt=&s->as.behave_s.methods;for(int i=0;i<mt->count;i++){char mn[256];snprintf(mn,sizeof(mn),"%s_%s",mangle(s->as.behave_s.type_name),mangle(mt->items[i].name));
            cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv){",mn);g->indent+=2;emit_fn_body(g,&mt->items[i].params,mt->items[i].body);g->indent-=2;cg_p(g,"}");cg_p(g,"");}}}
    // Emit lambda bodies (empty bodies by default — placeholder until proper capture lifting)
    for(int i=0;i<g->lambda_count;i++){
        char nm[64];snprintf(nm,sizeof(nm),"__lambda_%d",i);
        cg_p(g,"static KVal* fn_%s(KVal**_env,int _argc,KVal**_argv){",nm);g->indent+=2;
        emit_fn_body_caps(g,&g->lambdas[i].params,g->lambdas[i].body,g->lambdas[i].capture_count,g->lambdas[i].captures);
        g->indent-=2;cg_p(g,"}");cg_p(g,"");
    }
    g->lambda_count_final=g->lambda_count;
    // Emit enum variant constructor functions (top-level native C functions)
    for(Stmt*s=prog->first;s;s=s->next){if(s->kind==STMT_ENUM){
        for(int vi=0;vi<s->as.enum_s.variants.count;vi++){
            EnumVariant*v=&s->as.enum_s.variants.items[vi];
            int pc=v->fields.count;
            char fnn[128];snprintf(fnn,sizeof(fnn),"k_ev_%s",mangle(v->name));
            cg_p(g,"static KVal* %s(int _ac,KVal**_av){(void)_ac;",fnn);
            cg_i(g);fprintf(g->out,"KVal*_r=k_enum_new(\"%s\",%d",v->name,pc);
            for(int ai=0;ai<pc;ai++)fprintf(g->out,",k_retain(_av[%d])",ai);
            fprintf(g->out,");return _r;}\n");
        }
    }}
    t_i=0;a_i=0;cg_entry(g);fclose(f);
    if(g->had_error){fprintf(stderr,"krt codegen error: %s\n",g->error_msg);return 0;}return 1;
}
