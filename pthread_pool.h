#ifndef __MOD_PTHREAD_POOL__
#define __MOD_PTHREAD_POOL__

#define THREAD_COUNT    (2)
#define QUEUE_SIZE      (512)

void thread_pool_init(void);
void enqueue_task(void (*function)(void *), void *data);

#endif