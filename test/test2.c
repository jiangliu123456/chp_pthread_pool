#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "pthread_pool.h"
volatile int flag[3] = {0,0,0};
#define LOOP 100

void example_task1(void *data) {
    int *num = (int *)data;
    int j = -1;
    int k = 0;
    int t = 0;
    for	(int i = 0; i < 100; i++) {
        t = k;
        k = j;
        j = t;
    }
    __sync_fetch_and_add(&flag[0], 1);
}

void example_task2(void *data) {
    int *num = (int *)data;
    float a = 1.1;
    float b = 2.2;
    float c = 3.3;
    for	(int i = 0; i < 100; i++) {
        a = b*c;
        b++;
    }
    __sync_fetch_and_add(&flag[1], 1);
}

void example_task3(void *data) {
    int *num = (int *)data;
    long a = 4;
    long b = 5;
    long c = 6;
    for	(int i = 0; i < 100; i++) {
        a = a*a -1;
        b = b*b -4;
        c = c*c -3;
    }
    __sync_fetch_and_add(&flag[2], 1);
}

int main() {
    thread_pool_init();

    struct timespec ts;
    struct timespec ts2;
    int time[3] = {0,0,0};
    for (int i= 0; i < 100000; i++)
    {
        clock_gettime(CLOCK_REALTIME, &ts);
        int loop=LOOP;
        while(loop--)
        {
            
            int data1 = 1;
            enqueue_task(example_task1, &data1);
            
            while(flag[0] == flag[1])sleep(0);
            int data2 = 2;
            enqueue_task(example_task2, &data2);
            
            while(flag[1] == flag[2])sleep(0);
            int data3 = 3;
            enqueue_task(example_task3, &data3);
            // 等待任务执行完成
            // ...
        }
        while(flag[0]!=LOOP || flag[1]!=LOOP || flag[2]!=LOOP) {
            sleep(0);
        }
        flag[0] = 0;
        flag[1] = 0;
        flag[2] = 0;
        clock_gettime(CLOCK_REALTIME, &ts2);

        time[0] = (ts2.tv_sec-ts.tv_sec)*1000000+(ts2.tv_nsec/1000)-(ts.tv_nsec/1000);
        time[1]+=time[0];
        time[2] =time[1]/(i+1);
        printf("[loop:%04d]cur_time:%04d us,avg_time:%04d us\n", i+1,time[0], time[2]);
    }
    
    sleep(1);
    return 0;
}
