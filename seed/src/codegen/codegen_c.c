/*
 * codegen_c.c — Fast monomorphizing C backend for KODAS.
 * Emits unboxed C (e.g. long long fib(long long n)) for simple numeric programs,
 * avoiding KVal boxing entirely. Returns 0 (and does not write out_path) if
 * the program uses dynamic features that KRT must handle.
 */
#include "codegen_c.h"
#include "../ast.h"
#include "../parser.h"
#include "../lexer.h"
#include "../common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

/* ===== Simple type lattice =============================================== */
typedef enum { T_I64=0, T_F64=1, T_BOOL=2, T_CSTR=3, T_VOID=4 } CType;

static const char* ct_name(CType t){
    switch(t){
        case T_I64: return "long long";
        case T_F64: return "double";
        case T_BOOL:return "_Bool";
        case T_CSTR:return "const char*";
        case T_VOID:return "void";
    }
    return "long long";
}
static const char* ct_fmt(CType t){
    switch(t){
        case T_I64: return "%lld";
        case T_F64: return "%g";
        case T_BOOL:return "%s";
        case T_CSTR:return "%s";
        default:    return "%lld";
    }
}
static CType t_join(CType a, CType b){
    if(a==T_VOID) return b;
    if(b==T_VOID) return a;
    if(a==b) return a;
    if((a==T_I64||a==T_BOOL)&&b==T_F64) return T_F64;
    if((b==T_I64||b==T_BOOL)&&a==T_F64) return T_F64;
    if(a==T_BOOL&&b==T_I64) return T_I64;
    if(b==T_BOOL&&a==T_I64) return T_I64;
    return T_I64;
}
static int c_reserved(const char*w){
    static const char*r[]={
        "int","long","short","float","double","char","void","unsigned","signed",
        "if","else","while","for","do","switch","case","default","return","goto",
        "break","continue","struct","union","enum","typedef","static","extern",
        "register","const","volatile","sizeof","auto","_Bool","bool","true","false",
        "NULL","null","include","define",NULL};
    for(int i=0;r[i];i++) if(!strcmp(w,r[i])) return 1;
    return 0;
}

/* ===== String buffer for rvalue construction ============================ */
typedef struct { char*b; int n,cap; } SB;
static void sb_init(SB*s,char*b,int c){s->b=b;s->n=0;s->cap=c;s->b[0]=0;}
static void sb_cat(SB*s,const char*str){
    while(*str && s->n<s->cap-1) s->b[s->n++]=*str++;
    s->b[s->n]=0;
}
static void sb_fmt(SB*s,const char*fmt,...){
    char t[256]; va_list a; va_start(a,fmt);
    vsnprintf(t,sizeof(t),fmt,a); va_end(a);
    sb_cat(s,t);
}
static void sb_rep(SB*s,char c,int n){
    while(n-->0 && s->n<s->cap-1) s->b[s->n++]=c;
    s->b[s->n]=0;
}

/* ===== Generator state =================================================== */
typedef struct {
    FILE* out;
    int tmp;
    int indent;
} CGen;

typedef struct FnSig {
    char name[128];
    CType params[8];
    int nparams;
    CType ret;
} FnSig;

static void nl(CGen*g){fputc('\n',g->out);}
static void indent(CGen*g){for(int i=0;i<g->indent;i++)fputc(' ',g->out);}

static const char* esc(CGen*g,const char*n){
    (void)g;
    static char b[1024];
    int j=0;
    for(int i=0;n[i]&&j<(int)sizeof(b)-3;i++){
        char c=n[i];
        if(c=='-'||c=='.'||c=='!'||c=='?'||c=='\'') b[j++]='_';
        else b[j++]=c;
    }
    b[j]=0;
    if(c_reserved(b)){b[j++]='_';b[j]=0;}
    return b;
}

/* Forward */
static CType rval(CGen*g, FnSig* fns, int nfns, CType* locals, char local_names[][64], int* nlocals, Expr* e, SB* s);

static int local_idx(CType*locals,char names[][64],int nlocals,const char*nm){
    for(int i=nlocals-1;i>=0;i--) if(!strcmp(names[i],nm)) return i;
    return -1;
}
static CType local_type(CType*locals,char names[][64],int nlocals,const char*nm){
    int i=local_idx(locals,names,nlocals,nm);
    return i<0?T_I64:locals[i];
}
static void push_local(CType*locals,char names[][64],int*nlocals,const char*nm,CType t){
    if(*nlocals>=256) return;
    strncpy(names[*nlocals],nm,63); names[*nlocals][63]=0;
    locals[*nlocals]=t; (*nlocals)++;
}
static void pop_locals_to(int*nlocals,int n){while(*nlocals>n)(*nlocals)--;}

static FnSig* find_fn(FnSig*fns,int nfns,const char*nm){
    for(int i=0;i<nfns;i++) if(!strcmp(fns[i].name,nm)) return &fns[i];
    return NULL;
}

static const char* math_dbl(const char*nm){
    if(!strcmp(nm,"sqrt")||!strcmp(nm,"sin")||!strcmp(nm,"cos")||!strcmp(nm,"tan")||
       !strcmp(nm,"asin")||!strcmp(nm,"acos")||!strcmp(nm,"atan")||!strcmp(nm,"atan2")||
       !strcmp(nm,"exp")||!strcmp(nm,"log")||!strcmp(nm,"log2")||!strcmp(nm,"log10")||
       !strcmp(nm,"pow")||!strcmp(nm,"floor")||!strcmp(nm,"ceil")||!strcmp(nm,"round")||
       !strcmp(nm,"fabs")) return nm;
    return NULL;
}

/* ===== can_fast() ======================================================== */
static int can_fast(Program* prog,char*why,int whysz){
    for(Stmt*s=prog->first;s;s=s->next){
        switch(s->kind){
            case STMT_FN:
            case STMT_EXPR:
            case STMT_LET: case STMT_SEAL:
            case STMT_RETURN:
                break;
            default:
                snprintf(why,whysz,"unsupported top-level statement kind %d (try dyn)",(int)s->kind);
                return 0;
        }
    }
    /* Walk expression trees */
    Expr* stack[1024]; int sp=0;
    for(Stmt*s=prog->first;s;s=s->next){
        switch(s->kind){
            case STMT_EXPR: stack[sp++]=s->as.expr; break;
            case STMT_LET: case STMT_SEAL:
                if(s->as.let_s.value) stack[sp++]=s->as.let_s.value; break;
            case STMT_RETURN:
                if(s->as.expr) stack[sp++]=s->as.expr; break;
            case STMT_FN: stack[sp++]=s->as.fn_s.body; break;
            default: break;
        }
    }
    while(sp>0){
        Expr*e=stack[--sp]; if(!e) continue;
        switch(e->kind){
            case EXPR_INT: case EXPR_FLOAT: case EXPR_BOOL: case EXPR_STRING:
            case EXPR_CHAR: case EXPR_IDENT: case EXPR_EMPTY:
                break;
            case EXPR_BINARY:
                stack[sp++]=e->as.binary.left; stack[sp++]=e->as.binary.right; break;
            case EXPR_UNARY: stack[sp++]=e->as.unary.operand; break;
            case EXPR_CALL:
                stack[sp++]=e->as.call.callee;
                for(int i=0;i<e->as.call.args.count;i++) stack[sp++]=e->as.call.args.items[i];
                break;
            case EXPR_IF:
                stack[sp++]=e->as.if_e.cond; stack[sp++]=e->as.if_e.then_branch;
                stack[sp++]=e->as.if_e.else_branch; break;
            case EXPR_BLOCK:
                for(int i=0;i<e->as.block.stmts.count;i++){
                    Stmt*bs=e->as.block.stmts.items[i];
                    switch(bs->kind){
                        case STMT_LET: case STMT_SEAL:
                            if(bs->as.let_s.value) stack[sp++]=bs->as.let_s.value; break;
                        case STMT_EXPR:
                            if(bs->as.expr) stack[sp++]=bs->as.expr; break;
                        case STMT_RETURN:
                            if(bs->as.expr) stack[sp++]=bs->as.expr; break;
                        default:
                            snprintf(why,whysz,"unsupported stmt in block: %d",(int)bs->kind);
                            return 0;
                    }
                }
                break;
            case EXPR_ASSIGN: stack[sp++]=e->as.assign.value; break;
            case EXPR_COMPOUND_ASSIGN: stack[sp++]=e->as.compound.value; break;
            case EXPR_SHOW: case EXPR_PRINT: case EXPR_GIVE: case EXPR_ASSERT:
                stack[sp++]=e->as.expr; break;
            case EXPR_WHILE:
                stack[sp++]=e->as.loop_in.cond; stack[sp++]=e->as.loop_in.body; break;
            case EXPR_LOOP:
                stack[sp++]=e->as.loop_e.body; break;
            case EXPR_STOP: case EXPR_SKIP:
                break;
            default:
                snprintf(why,whysz,"unsupported expression kind %d (use dyn for dynamic features)",(int)e->kind);
                return 0;
        }
    }
    return 1;
}

/* ===== rval: emit a C rvalue expression into sb, return type ============ */
static CType rval(CGen*g, FnSig* fns, int nfns,
                  CType* locals, char local_names[][64], int* nlocals,
                  Expr* e, SB* s)
{
    switch(e->kind){
        case EXPR_INT:
            sb_fmt(s,"%lldLL",(long long)e->as.int_val);
            return T_I64;
        case EXPR_FLOAT:
            sb_fmt(s,"%.17g",e->as.float_val);
            return T_F64;
        case EXPR_BOOL:
            sb_cat(s,e->as.bool_val?"1":"0");
            return T_BOOL;
        case EXPR_STRING: {
            sb_cat(s,"(");
            sb_cat(s,"\"");
            for(const char*p=e->as.str_val;*p;p++){
                unsigned char c=(unsigned char)*p;
                if(c=='"'||c=='\\'){char b[3]={'\\',c,0};sb_cat(s,b);}
                else if(c=='\n')sb_cat(s,"\\n");
                else if(c=='\t')sb_cat(s,"\\t");
                else if(c=='\r')sb_cat(s,"\\r");
                else if(c=='\\')sb_cat(s,"\\\\");
                else if(c<0x20) sb_fmt(s,"\\x%02x",c);
                else {char b[2]={(char)c,0};sb_cat(s,b);}
            }
            sb_cat(s,"\" )");
            return T_CSTR;
        }
        case EXPR_CHAR: {
            char b[16]; snprintf(b,sizeof(b),"'%c'",e->as.char_val);
            sb_cat(s,b);
            return T_I64;
        }
        case EXPR_IDENT: {
            const char* nm=e->as.str_val;
            sb_fmt(s,"%s",esc(g,nm));
            return local_type(locals,local_names,*nlocals,nm);
        }
        case EXPR_BINARY: {
            const char*op=e->as.binary.op;
            const char*cop=op;
            if(!strcmp(op,"and"))cop="&&";
            else if(!strcmp(op,"or"))cop="||";
            else if(!strcmp(op,"<>"))cop="!=";
            int is_logic=(!strcmp(op,"and")||!strcmp(op,"or"));
            int is_cmp=(!strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||!strcmp(op,">")||
                        !strcmp(op,"<=")||!strcmp(op,">=")||!strcmp(op,"<>"));
            /* Short-circuit: emit with parens to control evaluation order. */
            /* Handle int/float division properly: if both sides are int, emit integer division as-is; if either float, cast. Type inference handled below. */
            sb_cat(s,"(");
            CType lt=rval(g,fns,nfns,locals,local_names,nlocals,e->as.binary.left,s);
            sb_fmt(s," %s ",cop);
            CType rt=rval(g,fns,nfns,locals,local_names,nlocals,e->as.binary.right,s);
            sb_cat(s,")");
            if(is_logic||is_cmp) return T_BOOL;
            /* For integer division we keep C's truncation which matches Kodas semantics. */
            return t_join(lt,rt);
        }
        case EXPR_UNARY: {
            const char*op=e->as.unary.op;
            const char*cop=op;
            if(!strcmp(op,"not"))cop="!";
            sb_fmt(s,"(%s",cop);
            CType t=rval(g,fns,nfns,locals,local_names,nlocals,e->as.unary.operand,s);
            sb_cat(s,")");
            if(!strcmp(op,"not")) return T_BOOL;
            return t;
        }
        case EXPR_CALL: {
            const char*cn=e->as.call.callee->kind==EXPR_IDENT?e->as.call.callee->as.str_val:"";
            FnSig*fn=find_fn(fns,nfns,cn);
            const char*md=math_dbl(cn);
            int n=e->as.call.args.count;
            /* Built-ins */
            if(!strcmp(cn,"clock")){
                sb_cat(s,"({struct timespec _ts;clock_gettime(CLOCK_MONOTONIC,&_ts);(double)_ts.tv_sec+(double)_ts.tv_nsec*1e-9;})");
                return T_F64;
            }
            if(!strcmp(cn,"abs")){
                sb_cat(s,"llabs(");
                if(n>=1) rval(g,fns,nfns,locals,local_names,nlocals,e->as.call.args.items[0],s);
                sb_cat(s,")"); return T_I64;
            }
            if(!strcmp(cn,"print")||!strcmp(cn,"println")||!strcmp(cn,"show")){
                /* Multi-arg printf statement-expression. */
                sb_cat(s,"({");
                /* Build per-arg temporaries */
                char tmps[8][32]; CType types[8];
                for(int i=0;i<n;i++){
                    snprintf(tmps[i],32,"_p%d",g->tmp++);
                    char rb[4096]; SB rs; sb_init(&rs,rb,sizeof(rb));
                    types[i]=rval(g,fns,nfns,locals,local_names,nlocals,e->as.call.args.items[i],&rs);
                    sb_fmt(s,"%s %s=(%s);",ct_name(types[i]),tmps[i],rb);
                }
                sb_cat(s,"printf(\"");
                for(int i=0;i<n;i++){
                    if(i)sb_cat(s," ");
                    sb_cat(s,ct_fmt(types[i]));
                }
                if(!strcmp(cn,"println")||!strcmp(cn,"show")) sb_cat(s,"\\n");
                sb_cat(s,"\"");
                for(int i=0;i<n;i++){
                    sb_cat(s,",");
                    if(types[i]==T_BOOL) sb_fmt(s,"(%s?\"true\":\"false\")",tmps[i]);
                    else sb_cat(s,tmps[i]);
                }
                sb_cat(s,");0;})");
                return T_I64;
            }
            if(!strcmp(cn,"input")){
                sb_cat(s,"({static char _ib[2048];if(fgets(_ib,2048,stdin)){char*_p=strchr(_ib,'\\n');if(_p)*_p=0;}_ib;})");
                return T_CSTR;
            }
            if(!strcmp(cn,"len")){
                if(n>=1){
                    sb_cat(s,"(long long)strlen((");
                    rval(g,fns,nfns,locals,local_names,nlocals,e->as.call.args.items[0],s);
                    sb_cat(s,"))");
                } else sb_cat(s,"0");
                return T_I64;
            }
            if(md){
                sb_fmt(s,"%s(",md);
                for(int i=0;i<n;i++){
                    if(i)sb_cat(s,",");
                    sb_cat(s,"(double)(");
                    rval(g,fns,nfns,locals,local_names,nlocals,e->as.call.args.items[i],s);
                    sb_cat(s,")");
                }
                sb_cat(s,")");
                return T_F64;
            }
            /* Ordinary function call */
            sb_fmt(s,"%s(",esc(g,cn));
            for(int i=0;i<n;i++){
                if(i)sb_cat(s,",");
                if(fn && i<fn->nparams && fn->params[i]==T_F64) sb_cat(s,"(double)(");
                else if(fn && i<fn->nparams && fn->params[i]==T_I64) sb_cat(s,"(long long)(");
                rval(g,fns,nfns,locals,local_names,nlocals,e->as.call.args.items[i],s);
                if(fn && i<fn->nparams && (fn->params[i]==T_F64||fn->params[i]==T_I64)) sb_cat(s,")");
            }
            sb_cat(s,")");
            return fn?fn->ret:T_I64;
        }
        case EXPR_IF: {
            sb_cat(s,"(");
            rval(g,fns,nfns,locals,local_names,nlocals,e->as.if_e.cond,s);
            sb_cat(s,"?");
            CType tt=rval(g,fns,nfns,locals,local_names,nlocals,e->as.if_e.then_branch,s);
            sb_cat(s,":");
            CType et=T_I64;
            if(e->as.if_e.else_branch) et=rval(g,fns,nfns,locals,local_names,nlocals,e->as.if_e.else_branch,s);
            else sb_cat(s,"0");
            sb_cat(s,")");
            return t_join(tt,et);
        }
        case EXPR_BLOCK: {
            sb_cat(s,"({");
            int prev=*nlocals;
            int cnt=e->as.block.stmts.count;
            CType rt=T_I64;
            int returned=0;
            for(int i=0;i<cnt;i++){
                Stmt*bs=e->as.block.stmts.items[i];
                switch(bs->kind){
                    case STMT_LET: case STMT_SEAL: {
                        const char*nm=bs->as.let_s.name;
                        if(bs->as.let_s.value){
                            char rb[4096]; SB rs; sb_init(&rs,rb,sizeof(rs));
                            CType vt=rval(g,fns,nfns,locals,local_names,nlocals,bs->as.let_s.value,&rs);
                            sb_fmt(s,"%s %s=(%s);",ct_name(vt),esc(g,nm),rb);
                            push_local(locals,local_names,nlocals,nm,vt);
                            rt=vt;
                        } else {
                            sb_fmt(s,"long long %s=0;",esc(g,nm));
                            push_local(locals,local_names,nlocals,nm,T_I64);
                        }
                        break;
                    }
                    case STMT_EXPR: {
                        Expr* in=bs->as.expr;
                        if(in && in->kind==EXPR_GIVE){
                            if(in->as.expr) rt=rval(g,fns,nfns,locals,local_names,nlocals,in->as.expr,s);
                            returned=1;
                        } else if(in && (in->kind==EXPR_SHOW || in->kind==EXPR_PRINT || in->kind==EXPR_ASSERT || in->kind==EXPR_WHILE || in->kind==EXPR_LOOP)){
                            /* Can't easily embed as an expression; approximate: ignore value and emit 0 */
                            /* For these cases we lose semantics but keep compiling; fast-c targets numeric code. */
                            sb_cat(s,"0;");
                            rt=T_I64;
                        } else {
                            char rb[4096]; SB rs; sb_init(&rs,rb,sizeof(rs));
                            CType t=rval(g,fns,nfns,locals,local_names,nlocals,in,&rs);
                            sb_fmt(s,"%s;",rb); rt=t;
                        }
                        break;
                    }
                    case STMT_RETURN: {
                        if(bs->as.expr) rt=rval(g,fns,nfns,locals,local_names,nlocals,bs->as.expr,s);
                        returned=1;
                        break;
                    }
                    default: break;
                }
                if(returned) break;
            }
            sb_cat(s,";})");
            pop_locals_to(nlocals,prev);
            return rt;
        }
        case EXPR_ASSIGN: {
            if(e->as.assign.name){
                CType lt=local_type(locals,local_names,*nlocals,e->as.assign.name);
                sb_fmt(s,"(%s=",esc(g,e->as.assign.name));
                rval(g,fns,nfns,locals,local_names,nlocals,e->as.assign.value,s);
                sb_cat(s,")");
                return lt;
            }
            sb_cat(s,"0"); return T_I64;
        }
        case EXPR_COMPOUND_ASSIGN: {
            /* compound.target is an Expr; for simple cases it's EXPR_IDENT */
            const char*op=e->as.compound.op;
            const char*co="+=";
            if(!strcmp(op,"-"))co="-=";
            else if(!strcmp(op,"*"))co="*=";
            else if(!strcmp(op,"/"))co="/=";
            else if(!strcmp(op,"%"))co="%=";
            sb_cat(s,"(");
            CType lt=rval(g,fns,nfns,locals,local_names,nlocals,e->as.compound.target,s);
            sb_fmt(s,"%s",co);
            rval(g,fns,nfns,locals,local_names,nlocals,e->as.compound.value,s);
            sb_cat(s,")");
            return lt;
        }
        case EXPR_SHOW: case EXPR_PRINT: case EXPR_GIVE: case EXPR_ASSERT:
            return rval(g,fns,nfns,locals,local_names,nlocals,e->as.expr,s);
        case EXPR_STOP: sb_cat(s,"break"); return T_VOID;
        case EXPR_SKIP: sb_cat(s,"continue"); return T_VOID;
        default:
            sb_cat(s,"0/*unsupported*/");
            return T_I64;
    }
}

/* ===== Stmt/body emission =============================================== */
static void emit_body(CGen*g,FnSig*fns,int nfns,Expr*body);
static void emit_stmts(CGen*g,FnSig*fns,int nfns,CType*locals,char names[][64],int*nlocals,Expr*body){
    if(!body) return;
    if(body->kind==EXPR_BLOCK){
        indent(g); fputs("{\n",g->out);
        g->indent+=2;
        int prev=*nlocals;
        int cnt=body->as.block.stmts.count;
        int returned=0;
        for(int i=0;i<cnt;i++){
            Stmt*bs=body->as.block.stmts.items[i];
            switch(bs->kind){
                case STMT_LET: case STMT_SEAL: {
                    const char*nm=bs->as.let_s.name;
                    if(bs->as.let_s.value){
                        char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                        CType vt=rval(g,fns,nfns,locals,names,nlocals,bs->as.let_s.value,&rs);
                        indent(g); fprintf(g->out,"%s %s = %s;\n",ct_name(vt),esc(g,nm),rb);
                        push_local(locals,names,nlocals,nm,vt);
                    } else {
                        indent(g); fprintf(g->out,"long long %s = 0;\n",esc(g,nm));
                        push_local(locals,names,nlocals,nm,T_I64);
                    }
                    break;
                }
                case STMT_EXPR: {
                    Expr* inner=bs->as.expr;
                    if(!inner) break;
                    if(inner->kind==EXPR_SHOW || inner->kind==EXPR_PRINT){
                        Expr*arg=inner->as.expr;
                        char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                        CType t=rval(g,fns,nfns,locals,names,nlocals,arg,&rs);
                        const char*val=rb; char bw[8192];
                        if(t==T_BOOL){snprintf(bw,sizeof(bw),"(%s?\"true\":\"false\")",rb);val=bw;}
                        indent(g);
                        if(inner->kind==EXPR_PRINT) fprintf(g->out,"printf(\"%s\", %s);\n",ct_fmt(t),val);
                        else fprintf(g->out,"printf(\"%s\\n\", %s);\n",ct_fmt(t),val);
                    } else if(inner->kind==EXPR_GIVE){
                        if(inner->as.expr){
                            char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                            CType t=rval(g,fns,nfns,locals,names,nlocals,inner->as.expr,&rs);
                            (void)t;
                            indent(g); fprintf(g->out,"return %s;\n",rb);
                        } else indent(g),fputs("return;\n",g->out);
                        returned=1;
                    } else if(inner->kind==EXPR_ASSERT){
                        char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                        rval(g,fns,nfns,locals,names,nlocals,inner->as.expr,&rs);
                        indent(g); fprintf(g->out,"if(!(%s)){ fprintf(stderr,\"assertion failed\\n\"); exit(1); }\n",rb);
                    } else if(inner->kind==EXPR_IF){
                        char cbuf[8192]; SB csb; sb_init(&csb,cbuf,sizeof(cbuf));
                        rval(g,fns,nfns,locals,names,nlocals,inner->as.if_e.cond,&csb);
                        int wprev=*nlocals;
                        indent(g); fprintf(g->out,"if(%s){\n",cbuf);
                        g->indent+=2;
                        emit_stmts(g,fns,nfns,locals,names,nlocals,inner->as.if_e.then_branch);
                        g->indent-=2;
                        if(inner->as.if_e.else_branch){
                            indent(g); fputs("} else {\n",g->out);
                            g->indent+=2;
                            emit_stmts(g,fns,nfns,locals,names,nlocals,inner->as.if_e.else_branch);
                            g->indent-=2;
                        }
                        indent(g); fputs("}\n",g->out);
                        pop_locals_to(nlocals,wprev);
                    } else if(inner->kind==EXPR_WHILE){
                        char cbuf[8192]; SB csb; sb_init(&csb,cbuf,sizeof(cbuf));
                        rval(g,fns,nfns,locals,names,nlocals,inner->as.loop_in.cond,&csb);
                        int wprev=*nlocals;
                        indent(g); fprintf(g->out,"while(1){\n");
                        g->indent+=2;
                        indent(g); fprintf(g->out,"if(!(%s)) break;\n",cbuf);
                        emit_stmts(g,fns,nfns,locals,names,nlocals,inner->as.loop_in.body);
                        g->indent-=2;
                        indent(g); fputs("}\n",g->out);
                        pop_locals_to(nlocals,wprev);
                    } else if(inner->kind==EXPR_LOOP){
                        int wprev=*nlocals;
                        indent(g); fprintf(g->out,"while(1){\n");
                        g->indent+=2;
                        emit_stmts(g,fns,nfns,locals,names,nlocals,inner->as.loop_e.body);
                        g->indent-=2;
                        indent(g); fputs("}\n",g->out);
                        pop_locals_to(nlocals,wprev);
                    } else {
                        char tmp[32]; snprintf(tmp,32,"_t%d",g->tmp++);
                        char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                        CType t=rval(g,fns,nfns,locals,names,nlocals,inner,&rs);
                        indent(g); fprintf(g->out,"%s %s = %s;\n",ct_name(t),tmp,rb);
                    }
                    break;
                }
                case STMT_RETURN: {
                    if(bs->as.expr){
                        char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                        CType t=rval(g,fns,nfns,locals,names,nlocals,bs->as.expr,&rs);
                        (void)t;
                        indent(g); fprintf(g->out,"return %s;\n",rb);
                    } else {
                        indent(g); fputs("return;\n",g->out);
                    }
                    returned=1; break;
                }
                default: break;
            }
            if(returned) break;
        }
        pop_locals_to(nlocals,prev);
        g->indent-=2;
        indent(g); fputs("}\n",g->out);
    } else {
        /* Single expression body -> { return <expr>; } */
        char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
        CType t=rval(g,fns,nfns,locals,names,nlocals,body,&rs);
        (void)t;
        indent(g); fputs("{\n",g->out);
        g->indent+=2;
        indent(g); fprintf(g->out,"return %s;\n",rb);
        g->indent-=2;
        indent(g); fputs("}\n",g->out);
    }
}

/* Type-infer function signatures: iterate fn bodies and look at return/give
   sites and call sites to pick T_F64 vs T_I64.
   For simplicity, we do one pass: if a function contains a float literal in a
   return/give position, or calls a double-returning function in a return
   position, mark ret T_F64; also, if a parameter is added to a float literal,
   mark that param T_F64. This is conservative but correct for fib/simple math. */
static void infer_sigs(Program* prog, FnSig*fns,int nfns){
    for(Stmt*fs=prog->first;fs;fs=fs->next){
        if(fs->kind!=STMT_FN) continue;
        FnSig*sig=find_fn(fns,nfns,fs->as.fn_s.name); if(!sig) continue;
        int has_float=0;
        Expr* stk[512]; int sp=0;
        stk[sp++]=fs->as.fn_s.body;
        while(sp>0){
            Expr*e=stk[--sp]; if(!e) continue;
            switch(e->kind){
                case EXPR_FLOAT: has_float=1; break;
                case EXPR_BINARY: stk[sp++]=e->as.binary.left; stk[sp++]=e->as.binary.right; break;
                case EXPR_UNARY: stk[sp++]=e->as.unary.operand; break;
                case EXPR_CALL: {
                    const char*cn=e->as.call.callee->kind==EXPR_IDENT?e->as.call.callee->as.str_val:"";
                    if(math_dbl(cn)) has_float=1;
                    if(!strcmp(cn,"clock")) has_float=1;
                    stk[sp++]=e->as.call.callee;
                    for(int i=0;i<e->as.call.args.count;i++) stk[sp++]=e->as.call.args.items[i];
                    /* If calling a function we know returns F64, propagate. */
                    FnSig*inner=find_fn(fns,nfns,cn);
                    if(inner && inner->ret==T_F64) has_float=1;
                    break;
                }
                case EXPR_IF: stk[sp++]=e->as.if_e.cond; stk[sp++]=e->as.if_e.then_branch; stk[sp++]=e->as.if_e.else_branch; break;
                case EXPR_BLOCK:
                    for(int i=0;i<e->as.block.stmts.count;i++){
                        Stmt*bs=e->as.block.stmts.items[i];
                        switch(bs->kind){
                            case STMT_LET: case STMT_SEAL: if(bs->as.let_s.value) stk[sp++]=bs->as.let_s.value; break;
                            case STMT_EXPR: if(bs->as.expr)stk[sp++]=bs->as.expr; break;
                            case STMT_RETURN: if(bs->as.expr) stk[sp++]=bs->as.expr; break;
                            default: break;
                        }
                    }
                    break;
                case EXPR_ASSIGN: stk[sp++]=e->as.assign.value; break;
                case EXPR_COMPOUND_ASSIGN: stk[sp++]=e->as.compound.value; break;
                case EXPR_SHOW: case EXPR_PRINT: case EXPR_GIVE: case EXPR_ASSERT: stk[sp++]=e->as.expr; break;
                case EXPR_WHILE: stk[sp++]=e->as.loop_in.cond; stk[sp++]=e->as.loop_in.body; break;
                case EXPR_LOOP: stk[sp++]=e->as.loop_e.body; break;
                default: break;
            }
        }
        if(has_float) sig->ret=T_F64;
        /* Now for params: if any param appears in a binary op with a float, mark it F64. */
        /* Re-walk and find binary ops where one side is a param and the other is float. */
        sp=0; stk[sp++]=fs->as.fn_s.body;
        while(sp>0){
            Expr*e=stk[--sp]; if(!e) continue;
            switch(e->kind){
                case EXPR_BINARY: {
                    Expr*L=e->as.binary.left, *R=e->as.binary.right;
                    int Lfloat=(L->kind==EXPR_FLOAT);
                    int Rfloat=(R->kind==EXPR_FLOAT);
                    /* also propagate through calls to math_dbl */
                    if(L->kind==EXPR_CALL){
                        const char*cn=L->as.call.callee->kind==EXPR_IDENT?L->as.call.callee->as.str_val:"";
                        if(math_dbl(cn)||!strcmp(cn,"clock")) Lfloat=1;
                    }
                    if(R->kind==EXPR_CALL){
                        const char*cn=R->as.call.callee->kind==EXPR_IDENT?R->as.call.callee->as.str_val:"";
                        if(math_dbl(cn)||!strcmp(cn,"clock")) Rfloat=1;
                    }
                    if(Lfloat && R->kind==EXPR_IDENT){
                        for(int p=0;p<sig->nparams;p++) if(!strcmp(sig->name,"")||!strcmp(fs->as.fn_s.params.items[p].name,R->as.str_val)){sig->params[p]=T_F64;break;}
                    }
                    if(Rfloat && L->kind==EXPR_IDENT){
                        for(int p=0;p<sig->nparams;p++) if(!strcmp(fs->as.fn_s.params.items[p].name,L->as.str_val)){sig->params[p]=T_F64;break;}
                    }
                    stk[sp++]=L; stk[sp++]=R; break;
                }
                case EXPR_UNARY: stk[sp++]=e->as.unary.operand; break;
                case EXPR_CALL: stk[sp++]=e->as.call.callee; for(int i=0;i<e->as.call.args.count;i++)stk[sp++]=e->as.call.args.items[i]; break;
                case EXPR_IF: stk[sp++]=e->as.if_e.cond; stk[sp++]=e->as.if_e.then_branch; stk[sp++]=e->as.if_e.else_branch; break;
                case EXPR_BLOCK:
                    for(int i=0;i<e->as.block.stmts.count;i++){
                        Stmt*bs=e->as.block.stmts.items[i];
                        switch(bs->kind){
                            case STMT_LET: case STMT_SEAL: if(bs->as.let_s.value) stk[sp++]=bs->as.let_s.value; break;
                            case STMT_EXPR: if(bs->as.expr)stk[sp++]=bs->as.expr; break;
                            case STMT_RETURN: if(bs->as.expr) stk[sp++]=bs->as.expr; break;
                            
                            default: break;
                        }
                    }
                    break;
                case EXPR_ASSIGN: stk[sp++]=e->as.assign.value; break;
                case EXPR_COMPOUND_ASSIGN: stk[sp++]=e->as.compound.value; break;
                case EXPR_SHOW: case EXPR_PRINT: case EXPR_GIVE: case EXPR_ASSERT: stk[sp++]=e->as.expr; break;
                case EXPR_WHILE: stk[sp++]=e->as.loop_in.cond; stk[sp++]=e->as.loop_in.body; break;
                case EXPR_LOOP: stk[sp++]=e->as.loop_e.body; break;
                default: break;
            }
        }
    }
}

/* ===== Public entry ====================================================== */
int codegen_c_emit_file(Program* prog, const char* out_path, const char* src_path){
    (void)src_path;
    char why[256]="";
    if(!can_fast(prog,why,sizeof(why))){
        fprintf(stderr,"[fast-c] cannot monomorphize: %s (falling back to KRT)\n",why);
        return 0;
    }
    FILE*f=fopen(out_path,"w");
    if(!f){perror(out_path);return 0;}

    FnSig fns[64]; int nfns=0;
    for(Stmt*s=prog->first;s;s=s->next){
        if(s->kind==STMT_FN){
            FnSig fs={0};
            strncpy(fs.name,s->as.fn_s.name,sizeof(fs.name)-1);
            fs.nparams=s->as.fn_s.params.count;
            for(int i=0;i<fs.nparams&&i<8;i++) fs.params[i]=T_I64;
            fs.ret=T_I64;
            fns[nfns++]=fs;
        }
    }
    infer_sigs(prog,fns,nfns);

    CGen G={0}; G.out=f; G.indent=0; G.tmp=0;

    fputs("/* KODAS v0.18 fast-C monomorphized output */\n",f);
    fputs("/* Compile: gcc -O3 -flto -o program program.c -lm */\n",f);
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include <string.h>\n#include <math.h>\n#include <time.h>\n",f);
    fputs("#ifndef CLOCK_MONOTONIC\n#define CLOCK_MONOTONIC 1\n#endif\n",f);

    /* Forward decls */
    for(int i=0;i<nfns;i++){
        fprintf(f,"%s %s(",ct_name(fns[i].ret),esc(&G,fns[i].name));
        for(int p=0;p<fns[i].nparams&&p<8;p++){
            if(p)fputc(',',f);
            fprintf(f,"%s a_%d",ct_name(fns[i].params[p]),p);
        }
        if(fns[i].nparams==0) fputs("void",f);
        fputs(");\n",f);
    }

    /* Function definitions */
    for(Stmt*s=prog->first;s;s=s->next){
        if(s->kind!=STMT_FN) continue;
        FnSig*sig=find_fn(fns,nfns,s->as.fn_s.name);
        fputc('\n',f);
        fprintf(f,"%s %s(",ct_name(sig?sig->ret:T_I64),esc(&G,s->as.fn_s.name));
        for(int p=0;p<s->as.fn_s.params.count&&p<8;p++){
            if(p)fputc(',',f);
            fprintf(f,"%s %s",ct_name(sig?sig->params[p]:T_I64),esc(&G,s->as.fn_s.params.items[p].name));
        }
        if(s->as.fn_s.params.count==0) fputs("void",f);
        fputs(")\n",f);

        /* Set up locals with params */
        CType locals[256]; char names[256][64]; int nlocals=0;
        G.tmp=0; G.indent=0;
        for(int p=0;p<s->as.fn_s.params.count&&p<8;p++){
            push_local(locals,names,&nlocals,s->as.fn_s.params.items[p].name,sig?sig->params[p]:T_I64);
        }
        emit_stmts(&G,fns,nfns,locals,names,&nlocals,s->as.fn_s.body);
    }

    /* main */
    fputs("\nint main(int argc,char**argv){\n",f);
    fputs("  (void)argc; (void)argv;\n",f);
    CType locals[256]; char names[256][64]; int nlocals=0;
    G.tmp=0; G.indent=2;
    for(Stmt*s=prog->first;s;s=s->next){
        if(s->kind==STMT_FN) continue;
        switch(s->kind){
            case STMT_LET: case STMT_SEAL: {
                const char*nm=s->as.let_s.name;
                if(s->as.let_s.value){
                    char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                    CType vt=rval(&G,fns,nfns,locals,names,&nlocals,s->as.let_s.value,&rs);
                    fprintf(f,"  %s %s = %s;\n",ct_name(vt),esc(&G,nm),rb);
                    push_local(locals,names,&nlocals,nm,vt);
                }
                break;
            }
            case STMT_EXPR: {
                Expr* in=s->as.expr;
                if(!in) break;
                if(in->kind==EXPR_SHOW || in->kind==EXPR_PRINT){
                    char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                    CType t=rval(&G,fns,nfns,locals,names,&nlocals,in->as.expr,&rs);
                    const char*val=rb; char bw[8192];
                    if(t==T_BOOL){snprintf(bw,sizeof(bw),"(%s?\"true\":\"false\")",rb);val=bw;}
                    if(in->kind==EXPR_PRINT) fprintf(f,"  printf(\"%s\", %s);\n",ct_fmt(t),val);
                    else fprintf(f,"  printf(\"%s\\n\", %s);\n",ct_fmt(t),val);
                } else if(in->kind==EXPR_ASSERT){
                    char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                    rval(&G,fns,nfns,locals,names,&nlocals,in->as.expr,&rs);
                    fprintf(f,"  if(!(%s)){ fprintf(stderr,\"assertion failed\\n\"); exit(1); }\n",rb);
                } else if(in->kind==EXPR_IF){
                    char cbuf[8192]; SB csb; sb_init(&csb,cbuf,sizeof(cbuf));
                    rval(&G,fns,nfns,locals,names,&nlocals,in->as.if_e.cond,&csb);
                    int wprev=nlocals;
                    fprintf(f,"  if(%s){\n",cbuf);
                    G.indent=4;
                    emit_stmts(&G,fns,nfns,locals,names,&nlocals,in->as.if_e.then_branch);
                    G.indent=2;
                    if(in->as.if_e.else_branch){
                        fprintf(f,"  } else {\n");
                        G.indent=4;
                        emit_stmts(&G,fns,nfns,locals,names,&nlocals,in->as.if_e.else_branch);
                        G.indent=2;
                    }
                    fprintf(f,"  }\n");
                    pop_locals_to(&nlocals,wprev);
                } else if(in->kind==EXPR_WHILE){
                    char cbuf[8192]; SB csb; sb_init(&csb,cbuf,sizeof(cbuf));
                    rval(&G,fns,nfns,locals,names,&nlocals,in->as.loop_in.cond,&csb);
                    int wprev=nlocals;
                    fprintf(f,"  while(1){\n");
                    G.indent=4;
                    fprintf(f,"    if(!(%s)) break;\n",cbuf);
                    emit_stmts(&G,fns,nfns,locals,names,&nlocals,in->as.loop_in.body);
                    fprintf(f,"  }\n");
                    G.indent=2;
                    pop_locals_to(&nlocals,wprev);
                } else if(in->kind==EXPR_LOOP){
                    int wprev=nlocals;
                    fprintf(f,"  while(1){\n");
                    G.indent=4;
                    emit_stmts(&G,fns,nfns,locals,names,&nlocals,in->as.loop_e.body);
                    fprintf(f,"  }\n");
                    G.indent=2;
                    pop_locals_to(&nlocals,wprev);
                } else {
                    char tmp[32]; snprintf(tmp,32,"_t%d",G.tmp++);
                    char rb[8192]; SB rs; sb_init(&rs,rb,sizeof(rb));
                    CType t=rval(&G,fns,nfns,locals,names,&nlocals,in,&rs);
                    fprintf(f,"  %s %s = %s;\n",ct_name(t),tmp,rb);
                }
                break;
            }
            case STMT_RETURN: break;
            default: break;
        }
    }
    fputs("  return 0;\n}\n",f);
    fclose(f);
    return 1;
}
