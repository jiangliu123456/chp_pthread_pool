/* main.cpp
 * Created by 一条晓鱼ovo on 2022/12/13.
 */
#include "hm_pthread_pool.h"
#if 1
#include <iostream>
#include <string>
#include <time.h>
#include <unistd.h>
int flag[3] = {0,0,0};
#define LOOP 100

void example_task1(void *data) {
    int *num = (int *)data;
    int j = -1;
    int k = 0;
    int t = 0;
    for	(int i = 0; i < 10; i++) {
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
    for	(int i = 0; i < 10; i++) {
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
    for	(int i = 0; i < 10; i++) {
        a = a*a -1;
        b = b*b -4;
        c = c*c -3;
    }
    __sync_fetch_and_add(&flag[2], 1);
}
/**************************class Task1******************************/
class Task {
public:
  typedef void (*func_type_t)(void*);
  Task() = default;

  explicit Task(std::string context, func_type_t func) { mContext = context; func_ = func; }

  bool operator<(const Task &e) const { return priority_ < e.priority_; }

  void Execute() {
    std::lock_guard<std::mutex> guard(mutex_);
    func_(NULL);
  }

public:
  uint32_t priority_;
  func_type_t func_;
private:
  std::string mContext;
  static std::mutex mutex_;
};

std::mutex Task::mutex_;

#define DEFAULT_THREAD_NUM (2)
#define MAX_THREAD_NUM     (2)
#define TIME_OUT           (1)

int test_threadpool() {
  static ThreadPool<Task, DEFAULT_THREAD_NUM, MAX_THREAD_NUM> threadPool(TIME_OUT);

  Task task1("task_1", example_task1);
  Task task2("task_2", example_task2);
  Task task3("task_3", example_task3);
  Task task4("task_4", example_task1);
  
  threadPool.Push(task1);
  threadPool.Push(task2);
  threadPool.Push(task3);
  threadPool.Push(task4);

  getchar();
  
  threadPool.Push(task1);
  threadPool.Push(task2);
  threadPool.Push(task3);
  threadPool.Push(task4);
  
  getchar();
  
  return 0;
}

#endif
int main() {

    static ThreadPool<Task, DEFAULT_THREAD_NUM, MAX_THREAD_NUM> threadPool(TIME_OUT);
    Task task1("task_1", example_task1);
    Task task2("task_2", example_task2);
    Task task3("task_3", example_task3);

    struct timespec ts;
    struct timespec ts2;
    int time[3] = {0,0,0};
    for (int i= 0; i < 100000; i++)
    {
        clock_gettime(CLOCK_REALTIME, &ts);
        int loop=LOOP;
        while(loop--)
        {
            threadPool.Push(task1);
            while(flag[0] == flag[1])sleep(0);
            threadPool.Push(task2);
            while(flag[1] == flag[2])sleep(0);
            threadPool.Push(task3);
            // 等待任务执行完成
            // ...
        }
        while(flag[0]!=LOOP || flag[1]!=LOOP || flag[2]!=LOOP) {
            sleep(0);//printf("%d %d %d\n", flag[0], flag[1], flag[2]);//sleep(0);
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
    
    //sleep(1);
    
    return 0;
}