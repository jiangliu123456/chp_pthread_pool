#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "pthread_pool.h"

typedef struct {
    void (*function)(void *);
    void *data;
} task_t;

task_t task_queue[QUEUE_SIZE];
int queue_front = 0, queue_rear = 0;

pthread_mutex_t queue_mutex;
pthread_cond_t queue_not_empty;
pthread_cond_t queue_not_full;

void *worker(void *arg) {
    while (1) {
        pthread_mutex_lock(&queue_mutex);
        while (queue_front == queue_rear) {
            pthread_cond_wait(&queue_not_empty, &queue_mutex);
        }
        task_t task = task_queue[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;
        pthread_cond_signal(&queue_not_full);
        pthread_mutex_unlock(&queue_mutex);
        (task.function)(task.data);
        
    }
    return NULL;
}

void thread_pool_init() {
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

void enqueue_task(void (*function)(void *), void *data) {
    pthread_mutex_lock(&queue_mutex);
    while ((queue_rear + 1) % QUEUE_SIZE == queue_front) {
        pthread_cond_wait(&queue_not_full, &queue_mutex);
    }
    task_queue[queue_rear].function = function;
    task_queue[queue_rear].data = data;
    queue_rear = (queue_rear + 1) % QUEUE_SIZE;
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_mutex);
}