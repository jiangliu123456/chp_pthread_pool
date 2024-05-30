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

int id[THREAD_COUNT]     = {0,1,2,3,4};//eventfd编号
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

    int water_line = (QUEUE_SIZE/THREAD_COUNT)/16;
    printf("worker %d start\n", id);
    memset(cnt_arry, 0, sizeof(cnt_arry));
    while (g_runing_stat == E_RUNNING_STAT_RUN) {
        /*count=0;阻塞*/
        re:
        while (_CAS_(&count,0,0)) {
            //条件等待倒计时,超过10次则pthread_cond_wait
            if (timerout_cnt >= timerout_max) {
                //printf("-------timerout_max = %d\n", timerout_max);
                FREE_LOCK(efd_w_lock);
                __sync_fetch_and_and(&efd_cnt, 1<<id);
                FREE_UNLOCK(efd_w_lock);
				int v;
                //printf("pthread_cond_wait %d fd=%d\n", id, evefd[id]);
                int _ = read(evefd[id], &v, sizeof(v));
                //printf("pthread_cond_wait %d over fd=%d v=%d\n", id, evefd[id],v);
                timerout_cnt = 0;
            } else {
                int _ = read(timerfd, &val, sizeof(val));//sleep 1us
                timerout_cnt++;
            }
        }
		
        FREE_LOCK(r_lock);
        //timerout_max 自适应算法
        if ((++cnt_idx & 7) ==0) {
            int h3 = cnt_idx>>3;
            if(8 == h3){
                cnt_idx = 0;
                h3 = 0;
            }
			//sum = sum +count - cnt_arry[h3];
            cnt_arry[h3] = count;
			int sum2 = 0;
			for(int j=0;j<8;j++)
				sum2+=cnt_arry[j];
            timerout_max = ((sum2>>3)>10)?10:1;
        }
        
        if (count==0) {
            FREE_UNLOCK(r_lock);
            goto re;
        }
        
        task_t task = task_queue[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;
        __sync_fetch_and_sub(&count, 1);
        
        FREE_UNLOCK(r_lock);

        (task.function)(task.data);
		timerout_cnt=0;
    }
    return NULL;
}

void thread_pool_init() {
    static int init_flag = 0;
    
    if (init_flag) {
        return;
    }
    
    int i = 0;
    for (i=0;i<THREAD_COUNT;i++) {
        evefd[i] = eventfd(0, /*EFD_NONBLOCK | */EFD_CLOEXEC|EFD_SEMAPHORE);
        if (evefd[i] == -1) {
            goto err_evefd;
        }
    }
    
    int j = 0;
    pthread_t threads[THREAD_COUNT];
    for (j = 0; j < THREAD_COUNT; j++) {
        if (0 != pthread_create(&threads[j], NULL, worker, &id[j]) ) {
            g_runing_stat = E_RUNNING_STAT_STOP;
            goto err_evefd;
        }
    }
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
		int k  = 0;
	    int temp = efd_cnt;
		while (temp) {
			if (temp&1) {
				break;
			}
			temp>>=1;
			k++;
		}
        int fd = evefd[k];

        if (-1 == write(fd, &fd, 4)) {
			FREE_UNLOCK(efd_w_lock);
			goto re_notify;
		}
        __sync_fetch_and_or(&efd_cnt, ~(1<<k));
        FREE_UNLOCK(efd_w_lock);
    }
}
