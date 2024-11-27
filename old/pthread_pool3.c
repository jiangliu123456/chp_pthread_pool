#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include "pthread_pool.h"

#define _CAS_(ptr, old, new)    __sync_bool_compare_and_swap(ptr, old, new)
#define FREE_LOCK(lock)   		while(!_CAS_(&(lock),0,1)) sleep(0)
#define FREE_UNLOCK(lock) 		lock = 0

typedef struct {
    void (*function)(void *);
    void *data;
} task_t;

task_t task_queue[QUEUE_SIZE];
int  queue_front = 0;
int  queue_rear  = 0;

volatile int  count             = 0;
volatile int  r_lock            = 0;
volatile int  w_lock            = 0;
volatile int  sleep_pthread_cnt = 0;

pthread_mutex_t queue_mutex;
pthread_cond_t queue_not_empty;
pthread_cond_t queue_not_full;

/*定时器函数,精度us*/
int set_timer_us(int s,int us)
{
    struct itimerspec timebuf;
    int timerfd = timerfd_create(CLOCK_MONOTONIC, /*TFD_NONBLOCK |*/ TFD_CLOEXEC);
    timebuf.it_interval.tv_sec  = s;//以后每次
    timebuf.it_interval.tv_nsec = us*1000;
    timebuf.it_value.tv_sec     = 0;//第一次
    timebuf.it_value.tv_nsec    = us*1000;

    timerfd_settime(timerfd, 0, &timebuf, NULL);

    return timerfd;
}

/*优化:减少pthread_mutex_lock和pthread_cond_wait的次数
如果队列为空,先倒计时10次,如果10次后仍为空,则pthread_cond_wait.
*/
void *worker(void *arg) 
{
    int timerfd = set_timer_us(0,1);
    unsigned long val;
    int timerout_cnt = 0;

    while (1) {
        /*count=0;阻塞*/
        re:
        while (_CAS_(&count,0,0)) {
            //条件等待倒计时,超过10次则pthread_cond_wait
            if (timerout_cnt>0) {
                pthread_mutex_lock(&queue_mutex);
                __sync_fetch_and_add(&sleep_pthread_cnt, 1);
                pthread_cond_wait(&queue_not_empty, &queue_mutex);
                pthread_mutex_unlock(&queue_mutex);
                timerout_cnt = 0;
                //printf("pthread_cond_wait over\n");
            } else {
                int _ = read(timerfd, &val, sizeof(val));//sleep 1us
                timerout_cnt++;
            }
        }

        timerout_cnt=0;
        FREE_LOCK(r_lock);
        if (count==0) {
            FREE_UNLOCK(r_lock);
            goto re;
        }
        
        task_t task = task_queue[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;
        __sync_fetch_and_sub(&count, 1);
        
        FREE_UNLOCK(r_lock);

        (task.function)(task.data);
        
    }
    return NULL;
}

void thread_pool_init() 
{
    static int init_flag = 0;
    
    if (init_flag) {
        return;
    }
    init_flag++;
    
    pthread_t threads[THREAD_COUNT];
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    pthread_cond_init(&queue_not_full, NULL);

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }
}

void enqueue_task(void (*function)(void *), void *data) 
{

    /*count=QUEUE_SIZE;阻塞*/
    re:
    while (_CAS_(&count,QUEUE_SIZE,QUEUE_SIZE)) {
        sleep(0);
    }
    FREE_LOCK(w_lock);
    if (count==QUEUE_SIZE){
        FREE_UNLOCK(w_lock);
        goto re;
    }
    task_queue[queue_rear].function = function;
    task_queue[queue_rear].data = data;
    queue_rear = (queue_rear + 1) % QUEUE_SIZE;
    __sync_fetch_and_add(&count, 1);
    
    if (!_CAS_(&sleep_pthread_cnt,0,0)) {
        pthread_mutex_lock(&queue_mutex);
        __sync_fetch_and_sub(&sleep_pthread_cnt, 1);
        pthread_cond_signal(&queue_not_empty);
        pthread_mutex_unlock(&queue_mutex);
    }
    FREE_UNLOCK(w_lock);
    
}