#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
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

int id[THREAD_COUNT]     = {0};//eventfd编号
int evefd[THREAD_COUNT];//eventfd fd保存
int epfd[THREAD_COUNT];
int efd_queue[THREAD_COUNT];//eventfd队列
volatile int efd_r       = 0;//efd_queue 读端index
volatile int efd_w       = 0;//efd_queue 写端index
volatile int efd_r_lock  = 0;//efd_queue 读端 锁
volatile int efd_w_lock  = 0;//efd_queue 写端 锁
volatile int efd_cnt     = 0;//efd_queue 成员个数

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
void* worker_dbg(void* arg)
{
    char str[100];
    while(1)
    {
        int rv = scanf("%s", str);
        if(str[0]='a'){
            printf("count=%d,efd_cnt=%d\n",count, efd_cnt);
        }
    }
    return NULL;
}
/*优化:减少pthread_mutex_lock和pthread_cond_wait的次数
如果队列为空,先倒计时10次,如果10次后仍为空,则阻塞.
*/
void *worker(void *arg) {
    int id = *((int*)arg);
    __sync_fetch_and_or(&efd_cnt, 1<<id);
    // 3.struct epoll_event event
    // EPOLL_CTL_ADD: //  将关心的文件描述符加入到红黑树上
    // EPOLLIN  :可读事件
    // EPOLLET：边沿检测
    // 初始化对应文件描述符
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = evefd[id];
    if (epoll_ctl(epfd[id], EPOLL_CTL_ADD, evefd[id], &event) < 0) {
        printf("epoll_ctl error\n");
    }

    while(g_runing_stat == E_RUNNING_STAT_INIT) sleep(0);
    if(g_runing_stat == E_RUNNING_STAT_STOP) return NULL;
    
    int timerfd = set_timer_us(0,1);
    unsigned long val;
    int timerout_cnt = 0;
    
    int v,ret;
    eventfd_t event_arg;

    printf("worker %d start efd_cnt=%d\n", id, efd_cnt);

    struct epoll_event events[1];
    while (g_runing_stat != E_RUNNING_STAT_STOP) {
        ret = 1;
        if(count==0) {
            __sync_fetch_and_or(&efd_cnt, (1<<id));
            if(_CAS_(&count,0,0))
            {
                ret = epoll_wait(epfd[id], events, 1, -1);
                FREE_LOCK(efd_w_lock);
                __sync_fetch_and_and(&efd_cnt, ~(1<<id));
                FREE_UNLOCK(efd_w_lock);
                v = eventfd_read(evefd[id],&event_arg);
            }
        }

        if (ret > 0) {
            
        re:
            while (count) 
            {
                FREE_LOCK(r_lock);
                if (_CAS_(&count,0,0)) {
                    FREE_UNLOCK(r_lock);
                    goto re;
                }
                
                task_t task = task_queue[queue_front];
                queue_front = (queue_front + 1) % QUEUE_SIZE;
                __sync_sub_and_fetch(&count, 1);
                
                FREE_UNLOCK(r_lock);

                (task.function)(task.data);
            }

            int k = 10;
            while(count==0 && k--){
                v = read(timerfd, &val, sizeof(val));
                //sleep(0);
            }
        }
    }

    return NULL;
}

void thread_pool_init() {
    static int init_flag = 0;
    int i = 0;
    int j = 0;

    if (init_flag) {
        return;
    }
    
    for (j=0;j<THREAD_COUNT;j++) {
        epfd[j] = epoll_create(1);
        if (epfd[j] < 0) {
            goto err_epfd;
        }
    }

    for (i=0;i<THREAD_COUNT;i++) {
        id[i]=i;
        evefd[i] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC/*|EFD_SEMAPHORE*/);
        printf("evefd[%d] = %d\n", i, evefd[i]);
        if (evefd[i] == -1) {
            goto err_evefd;
        }
    }
    
    int k = 0;
    pthread_t threads[THREAD_COUNT+1];
    for (k = 0; k<THREAD_COUNT; k++) {
        if (0 != pthread_create(&threads[k], NULL, worker, &id[k]) ) {
            g_runing_stat = E_RUNNING_STAT_STOP;
            goto err_evefd;
        }
    }
     pthread_create(&threads[k], NULL, worker_dbg, NULL);
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
err_epfd:
    while(j>=0) {
        if (epfd[j]>0) {
            close(epfd[j]);
        }
        j--;
    }
    printf("[%s][%d] error!!!\n", __FUNCTION__, __LINE__);
    return;
}

void enqueue_task(void (*function)(void *), void *data) {

    /*count=QUEUE_SIZE;阻塞*/
    int k  = 0;
    int temp = efd_cnt;
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
    k = 0;
    re_notify:
    if (!_CAS_(&efd_cnt,0,0)) {
        FREE_LOCK(efd_w_lock);
	    temp = efd_cnt;
		while (temp) {
			if (temp&1) {
				break;
			}
			temp>>=1;
			k++;
		}

        if (-1 == eventfd_write(evefd[k],1)) {
			FREE_UNLOCK(efd_w_lock);
            //printf("goto\n");
			goto re_notify;
		}
        
        //printf("[write] count = %d, efd_cnt = %d\n", count, efd_cnt);
        FREE_UNLOCK(efd_w_lock);
    }
}
