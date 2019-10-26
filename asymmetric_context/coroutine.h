#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#define COROUTINE_DEAD 0
#define COROUTINE_READY 1
#define COROUTINE_RUNNING 2
#define COROUTINE_SUSPEND 3

struct schedule;

typedef void (*coroutine_func)(struct schedule *, void *ud);

struct schedule * coroutine_open(int max_coroutine_num);
void coroutine_close(struct schedule *);

int coroutine_new(struct schedule *, coroutine_func, void *ud,int stack_size);
void coroutine_resume(struct schedule *, int id);
int coroutine_status(struct schedule *, int id);
int coroutine_running(struct schedule *);
void coroutine_yield(struct schedule *);

#endif
