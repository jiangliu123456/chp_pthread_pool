#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

#include "pthread_pool.h"

#define _CAS_(ptr, old, new)    __sync_bool_compare_and_swap(ptr, old, new)
#define FREE_LOCK(lock)         while(!_CAS_(&(lock),0,1)) sleep(0)
#define FREE_UNLOCK(lock)       lock = 0

typedef struct {
    void (*function)(void *);
    void *data;
} task_t;

enum {
    E_RUNNING_STAT_INIT  = -1,//init阶段
    E_RUNNING_STAT_STOP  = 0, //停止状态
    E_RUNNING_STAT_RUN   = 1, //运行状态
};

volatile int g_runing_stat = E_RUNNING_STAT_INIT;//线程运行状态标识

task_t task_queue[QUEUE_SIZE];
int  queue_front = 0;
int  queue_rear  = 0;
volatile int  count      = 0;//task_queue 成员个数
volatile int  r_lock     = 0;//task_queue 读锁
volatile int  w_lock     = 0;//task_queue 写锁

int id[THREAD_COUNT];//eventfd编号
int evefd[THREAD_COUNT];//eventfd fd保存
int efd_queue[THREAD_COUNT];//eventfd队列
volatile int efd_r       = 0;//efd_queue 读端index
volatile int efd_w       = 0;//efd_queue 写端index
volatile int efd_r_lock  = 0;//efd_queue 读端 锁
volatile int efd_w_lock  = 0;//efd_queue 写端 锁
volatile int efd_cnt     = 0;//efd_queue 成员个数

volatile int cnt_idx     = 0;
volatile int cnt_arry[8];
volatile int sum = 0;
volatile int timerout_max = 0;

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

void *worker_debug(void *arg){
    char str[10]={0};
    int rv = scanf("%s", str);
    if (str[0] == 's') {
    printf("task count:%d {w_lock=%d,r_lock=%d}\n",count,w_lock,r_lock);
    printf("efd_cnt :%#x\n", efd_cnt);
    } 
    if (str[0] == 'c')
    {
        g_runing_stat = E_RUNNING_STAT_STOP;
    }
    return;
}

/*优化:减少pthread_mutex_lock和pthread_cond_wait的次数
如果队列为空,先倒计时10次,如果10次后仍为空,则阻塞.
*/
void *worker(void *arg) {
    
    while(g_runing_stat == E_RUNNING_STAT_INIT) sleep(0);
    if(g_runing_stat == E_RUNNING_STAT_STOP) return NULL;
    
    int timerfd = set_timer_us(0,1);
    unsigned long val;
    int timerout_cnt = 0;
    int id = *((int*)arg);
    int ret;
    int k = 10;
    int i = 0;
    eventfd_t event_arg;
    task_t task[32];

    __sync_fetch_and_or(&efd_cnt, 1<<id);
    printf("worker %d start efd_cnt=%#x\n", id, efd_cnt);
    memset(cnt_arry, 0, sizeof(cnt_arry));
    
    while (g_runing_stat == E_RUNNING_STAT_RUN) {
    re:

        while (!_CAS_(&count,0,0)) {
            
            FREE_LOCK(r_lock);
            if (count == 0) {
                FREE_UNLOCK(r_lock);
                goto re;
            }
            
            k = 1;
            if (count > (QUEUE_SIZE>>3)) {
                k = QUEUE_SIZE>>5;// 取 16/1 QUEUE_SIZE
            }
            
            for (i = 0; i < k; i++) {
                task[i].function = task_queue[queue_front].function;
                task[i].data = task_queue[queue_front].data;
                queue_front = (queue_front + 1) % QUEUE_SIZE;
            }
            __sync_fetch_and_sub(&count, i);
        
            FREE_UNLOCK(r_lock);
            
            for (i = 0; i < k; i++) {
                (task[i].function)(task[i].data);
            }
        }

        while (count == 0 && k) {
            ret = read(timerfd, &val, sizeof(val));
            k--;
        }
        
        if (count == 0) {
            FREE_LOCK(efd_w_lock);
            __sync_fetch_and_or(&efd_cnt, (1<<id));
            FREE_UNLOCK(efd_w_lock);
            
            if (_CAS_(&count,0,0))
                ret = eventfd_read(evefd[id], &event_arg);
        }

    }
    return NULL;
}

void thread_pool_init() 
{
    static int init_flag = 0;
    
    if (init_flag) {
        return;
    }
    
    int i = 0;
    for (i=0;i<THREAD_COUNT;i++) {
        id[i] = i;
        evefd[i] = eventfd(0, /*EFD_NONBLOCK | */EFD_CLOEXEC/*|EFD_SEMAPHORE*/);
        if (evefd[i] == -1) {
            goto err_evefd;
        }
    }
    
    int j = 0;
    pthread_t threads[THREAD_COUNT+1];
    for (j = 0; j < THREAD_COUNT; j++) {
        if (0 != pthread_create(&threads[j], NULL, worker, &id[j]) ) {
            g_runing_stat = E_RUNNING_STAT_STOP;
            goto err_evefd;
        }
    }
    pthread_create(&threads[j], NULL, worker_debug, &id[0]);
    
    g_runing_stat = E_RUNNING_STAT_RUN;
    init_flag = 1;
    
    return;

    err_evefd:
    while(i>=0) {
        if (evefd[i]>0) {
            close(evefd[i]);
        }
        i--;
    }
    printf("[%s][%d] error!!!\n", __FUNCTION__, __LINE__);
    return;
}

void enqueue_task(void (*function)(void *), void *data) {

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
    FREE_UNLOCK(w_lock);
	
re_notify:
    if (!_CAS_(&efd_cnt,0,0)) {
        FREE_LOCK(efd_w_lock);
        if (efd_cnt==0) {
            FREE_UNLOCK(efd_w_lock);
            goto re_notify;
        }
		int k  = 0;
	    int temp = efd_cnt;
		while (temp) {
			if (temp&1) {
				break;
			}
			temp>>=1;
			k++;
		}

        if (-1 == eventfd_write(evefd[k], 1)) {
			FREE_UNLOCK(efd_w_lock);
			goto re_notify;
		}
        __sync_fetch_and_and(&efd_cnt, ~(1<<k));
        FREE_UNLOCK(efd_w_lock);
    }
}
