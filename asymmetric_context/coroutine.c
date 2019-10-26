#include  <stdio.h>
#include  <stdlib.h>
#include  <assert.h>
#include  <stddef.h>
#include  <string.h>
#include  <stdint.h>
#include  <ucontext.h>
#include  "coroutine.h"

#define    DEFAULT_STACK_SIZE    (1024*1024)
#define    DEFAULT_COROUTINE_MAX    16

struct  coroutine;

struct    schedule  {
        ucontext_t  main;
        int  nco;
        int  cap;
        int  running;
        struct  coroutine  **co;
};

struct    coroutine  {
    int  id;
    coroutine_func  func;
    void  *ud;
    ucontext_t  ctx;
    struct    schedule  *  sch;
    ptrdiff_t  cap;
    ptrdiff_t  size;
    int  status;
    char  *stack;
    ucontext_t pre_ctx;
};

struct  coroutine  *  co_new(coroutine_func  func,  void  *ud,  int  stack_size)  {
    struct  coroutine  *  co  =  malloc(sizeof(*co));
    co->func  =  func;
    co->ud  =  ud;
    co->cap  =  0;
    co->size  =  0;
    co->status  =  COROUTINE_READY;
    co->stack  =  malloc(stack_size);
    return    co;
}

void  co_delete(struct schedule  *S,struct  coroutine  *co){
    S->co[co->id]  =  NULL;
    S->nco--;
    free(co->stack);
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
    struct    coroutine    *co    =    co_new(func    ,    ud,  stack_size);

    if    (S->nco    >=    S->cap)    {
        S->co    =    realloc(S->co,    S->cap    *    2    *    sizeof(struct    coroutine    *));
        memset(S->co    +    S->cap    ,    0    ,    sizeof(struct    coroutine    *)    *    S->cap);
        S->cap    *=    2;
    }

    int    i;
    for    (i=0;i<S->cap;i++)    {
        int    id    =    (i+S->nco)    %    S->cap;
        if    (S->co[id]    ==    NULL)    {
            S->co[id]    =    co;
            ++S->nco;
            co->id  =  id;
            return    id;
        }
    }
    return    -1;
}

static  void mainfunc(uint32_t  low32,  uint32_t  hi32)  {
  uintptr_t  ptr  =  (uintptr_t)low32  |  ((uintptr_t)hi32  <<  32);
  struct  schedule  *S  =  (struct  schedule  *)ptr;
  int  id  =  S->running;
  struct  coroutine  *C  =  S->co[id];
  C->func(S,C->ud);
  co_delete(S,C);
  S->running  =  -1;
}

void  coroutine_resume(struct  schedule    *    S,    int    id)    {
    assert(id >=0 && id < S->cap);
    struct coroutine *C = S->co[id];

    ucontext_t* uc = &S->main;
    if(S->running>=0){
      struct coroutine *cur_c = S->co[S->running];
      uc = &C->pre_ctx;
      getcontext(uc);
    }

    if(C == NULL)
        return;
    int status = C->status;
    switch(status) {
        case COROUTINE_READY:
            getcontext(&C->ctx);
            C->ctx.uc_stack.ss_sp = C->stack;
            C->ctx.uc_stack.ss_size = C->size;
            C->ctx.uc_link = uc;
            S->running = id;
            C->status = COROUTINE_RUNNING;
            uintptr_t ptr = (uintptr_t)S;
            makecontext(&C->ctx, (void(*)(void))mainfunc,2,(uint32_t)ptr,(uint32_t)(ptr>>32));
            swapcontext(uc, &C->ctx);
            break;
        case COROUTINE_SUSPEND:
            S->running = id;
            C->ctx.uc_link = uc;
            C->status = COROUTINE_RUNNING;
            swapcontext(uc, &C->ctx);
            break;
        default:
            assert(0);
    }
}

void  coroutine_yield(struct    schedule    *    S)    {
    int id = S->running;
    assert(id >= 0);
    struct coroutine * C = S->co[id];
    C->status = COROUTINE_SUSPEND;
    S->running = -1;
    swapcontext(&C->ctx , &C->pre_ctx);
}

int  coroutine_status(struct    schedule    *    S,    int    id)    {
    assert(id>=0    &&    id    <    S->cap);
    if    (S->co[id]    ==    NULL)    {
        return COROUTINE_DEAD;
    }
    return S->co[id]->status;
}

int  coroutine_running(struct    schedule    *    S)    {
    return    S->running;
}
