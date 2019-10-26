#include<stdio.h>
#include "coroutine.h"

int co1,co2;

void co_f2(struct schedule* s,void* ud){
    puts("before yeild");
    coroutine_yield(s);
    puts("after yeild");
}

void co_f1(struct schedule* s,void* ud){
    puts("resume 1");
    coroutine_resume(s,co2);

    puts("resume 2");
    coroutine_resume(s,co2);
    
    puts("co1 finish");
}

int main(){
  struct  schedule  *S  =  coroutine_open(100);

  co1 = coroutine_new(S,co_f1,NULL,0);
  
  co2 = coroutine_new(S,co_f2,NULL,0);

  coroutine_resume(S,co1);

  coroutine_close(S);

  return 0;
}
