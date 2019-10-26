#include  <stdio.h>
#include  <stdlib.h>
#include  <assert.h>
#include  <stddef.h>
#include  <string.h>
#include  <stdint.h>
#include  <ucontext.h>
#include <setjmp.h>
#include <sys/mman.h>
#include  "coroutine.h"

#define    DEFAULT_STACK_SIZE    (1024*1024)
#define    DEFAULT_COROUTINE_MAX    16


#include <sys/mman.h>

#define PAGE_SIZE  (4096)
#define STACK_SIZE (2048*PAGE_SIZE)
#define JB(a,b) ((a)[0].__jmpbuf[(b)])

enum jb {JB_RBX,JB_RBP,JB_R12,JB_R13,JB_R14,JB_R15,JB_RSP,JB_PC};
typedef long long int jb_int_t;

jb_int_t translate_address(jb_int_t addr)
{
    jb_int_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
		"rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

struct  coroutine;

struct schedule {
    int  nco;
    int  cap;
    int  running;
    jmp_buf jb;
    struct  coroutine  **co;
};

struct coroutine  {
    int id;
    coroutine_func func;
    void *ud;
    jmp_buf jb;
    struct schedule * sch;
    ptrdiff_t cap;
    ptrdiff_t size;
    int status;
    char *stack;
    jmp_buf* pre_jb;
};

static  void mainfunc();

struct  coroutine * co_new(struct schdule* S,coroutine_func func, void *ud, int stack_size) {
    struct  coroutine* co = malloc(sizeof(*co));
    co->func =  func;
    co->ud =  ud;
    co->cap =  0;
    co->size =  0;
    co->status =  COROUTINE_READY;
    co->stack = malloc(STACK_SIZE);

    void *stack = mmap(NULL, STACK_SIZE, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    mprotect(stack, PAGE_SIZE, PROT_NONE);
    mprotect(stack+STACK_SIZE-PAGE_SIZE, PAGE_SIZE, PROT_NONE);
    co->stack = stack;
    JB(co->jb, JB_PC)  = translate_address((jb_int_t)mainfunc);
    JB(co->jb, JB_RSP) = translate_address(stack+STACK_SIZE-PAGE_SIZE);
//    JB(co->jb, JB_RBP) = translate_address(stack+STACK_SIZE-PAGE_SIZE);
    JB(co->jb, JB_RBX) = (jb_int_t)S;
    JB(co->jb, JB_R12) = (jb_int_t)ud;
    return co;
}

void  co_delete(struct schedule  *S,struct  coroutine  *co){
    S->co[co->id]  =  NULL;
    S->nco--;
    munmap(co->stack,STACK_SIZE);
    free(co);
}

struct  schedule  *  coroutine_open(int  max_coroutine)  {
    struct  schedule  *S  =  malloc(sizeof(*S));
    S->nco  =  0;
    if(max_coroutine  >  0){
        S->cap  =  max_coroutine;
    }  else  {
        S->cap  =  DEFAULT_COROUTINE_MAX;
    }
    S->running  =  -1;
    S->co  =  malloc(sizeof(struct  coroutine  *)  *  S->cap);
    memset(S->co,  0,  sizeof(struct  coroutine  *)  *  S->cap);
    return  S;
}

void  coroutine_close(struct    schedule    *S)    {
    int    i;
    for    (i=0;i<S->cap;i++)    {
        struct    coroutine    *    co    =    S->co[i];
        if    (co)    {
            co_delete(S,co);
        }
    }
    free(S->co);
    S->co    =    NULL;
    free(S);
}

int  coroutine_new(struct    schedule    *S,    coroutine_func    func,    void    *ud,  int  stack_size)    {
    struct coroutine *co = co_new(S,func , ud, stack_size);

    if(S->nco >= S->cap) {
        S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
        memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
        S->cap    *=    2;
    }

    int i;
    for (i=0;i<S->cap;i++)    {
        int id = (i+S->nco) % S->cap;
        if (S->co[id] == NULL){
            S->co[id] = co;
            ++S->nco;
            co->id  =  id;
            return    id;
        }
    }
    return -1;
}

static  void mainfunc()  {
  register void * ptr asm("rbx");
  struct  schedule  *S  =  (struct  schedule  *)ptr;
  int  id  =  S->running;
  struct  coroutine  *C  =  S->co[id];
  C->func(S,C->ud);
  jmp_buf* jb = C->pre_jb;
  S->running  =  -1;
  C->status = COROUTINE_DEAD;
  longjmp(jb,1);
}

void  coroutine_resume(struct schedule* S,int id) {
    assert(id >=0 && id < S->cap);
    struct coroutine *C = S->co[id];

    if(C == NULL) return;
    int status = C->status;
    if(status != COROUTINE_READY && status != COROUTINE_SUSPEND){
        return;
    }

    int ret;
    struct jmp_buf* jb = &S->jb;
    if(S->running>=0){
        struct coroutine* pre_c = S->co[S->running];
        C->pre_jb = &pre_c->jb;
        if(setjmp(pre_c->jb)){
            if(C->status == COROUTINE_DEAD){ 
              co_delete(S,C);
            }
            return; 
        }
    }else{
        C->pre_jb = &S->jb;
        if(setjmp(S->jb)){
            if(C->status == COROUTINE_DEAD){ 
              co_delete(S,C);
            }
            return; 
        }
    }

    S->running = C->id;
    longjmp(C->jb,1);
}

void  coroutine_yield(struct    schedule    *    S)    {
    int id = S->running;
    struct coroutine * C = S->co[id];
    S->running = -1;
    if(setjmp(C->jb)){
        longjmp(*C->pre_jb,1);
    }
}

int  coroutine_status(struct schedule * S, int id) {
    assert(id>=0  && id < S->cap);
    if (S->co[id] == NULL) {
        return COROUTINE_DEAD;
    }
    return S->co[id]->status;
}

int  coroutine_running(struct    schedule    *    S)    {
    return S->running;
}
