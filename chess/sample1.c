#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;

void* thread1(void* arg);
void* thread2(void* arg);

int main()
{
    pthread_t thread;
    pthread_create(&thread, NULL, thread2, NULL); 
     // printf("The ID of this thread is: %u\n", (unsigned int)pthread_self());
        printf("JOINCALL: %ld\n",(long)thread);
     thread1(0);
    pthread_join(thread, NULL);
    
    return 0;
}

void* thread1(void* arg)
{
printf("START 1: %ld\n",(long)pthread_self());
	sleep(1);
    puts ("thread1-1");
    pthread_mutex_lock(&mutex1);
    puts ("thread1-2");
    sched_yield();
    pthread_mutex_lock(&mutex2);
    puts ("thread1-3");
    pthread_mutex_unlock(&mutex2);
    puts ("thread1-4");
    pthread_mutex_unlock(&mutex1);
}

void* thread2(void* arg)
{
	printf("START 2: %ld\n",(long)pthread_self());
    puts ("thread2-1");
    pthread_mutex_lock(&mutex2);
    puts ("thread2-2");
    sleep(2);
    sched_yield();
    pthread_mutex_unlock(&mutex2);
    puts ("thread2-3");
    pthread_mutex_lock(&mutex1);
    puts ("thread2-4");
    pthread_mutex_unlock(&mutex1);
}
