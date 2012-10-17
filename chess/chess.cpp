#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <dlfcn.h>
#include <stdio.h>
#include <map>
#include <queue>

//thread lock states
#define NOTWAIT 1
#define WAIT 2
#define DEAD 3

int (*original_pthread_create)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*) = NULL;
int (*original_pthread_join)(pthread_t, void**) = NULL;
int (*original_pthread_mutex_lock)(pthread_mutex_t*) = NULL;
int (*original_pthread_mutex_unlock)(pthread_mutex_t*) = NULL;
extern "C" void chess_schedule(void);

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

static int sync_count = 0;
FILE* syncfile = NULL;

int firstrun = 1;
long gl_holder = 0; //thread with global lock

long threads[2] = {0,0};
int status[2] = {1, 1};

static void initialize_original_functions();

struct Thread_Arg {
    void* (*start_routine)(void*);
    void* arg;
};

static
void* thread_main(void *arg)
{
    struct Thread_Arg thread_arg = *(struct Thread_Arg*)arg;
    free(arg);
    //printf("thread MAIN: %ld\n",(long)pthread_self());
    int me = 0;
    if(threads[0] == 0)
    {
        threads[0] = (long)pthread_self();
        me = 0;
    }else
    {
        threads[1] = (long)pthread_self();
        me = 1;
    }

    original_pthread_mutex_lock(&global_lock);
    gl_holder = (long)pthread_self();
    chess_schedule();
    void* rc = thread_arg.start_routine(thread_arg.arg);
    status[me] = 3;

    
    
    //original_pthread_mutex_unlock(&global_lock);
    original_pthread_mutex_unlock(&global_lock);
    fprintf(stderr, "exiting");
    return rc;
}

extern "C"
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg)
{

    initialize_original_functions();
    //original_pthread_mutex_unlock(&global_lock);
    //original_pthread_mutex_lock(&global_lock);
    struct Thread_Arg *thread_arg = (struct Thread_Arg*)malloc(sizeof(struct Thread_Arg));
    thread_arg->start_routine = start_routine;
    thread_arg->arg = arg;
    if(firstrun)
    {
    	firstrun = 0;
    	original_pthread_mutex_lock(&global_lock);
    	gl_holder = (long)pthread_self();
        if(threads[0] == 0)
        {
            threads[0] = gl_holder;
        }else
        {
            threads[1] = gl_holder;
        }   

    	//printf("firstrun self: %ld\n",(long)pthread_self());
    	//printf("firstrun thread: %ld\n",(long)thread);
    	
    }
    int ret = original_pthread_create(thread, attr, thread_main, thread_arg);  
    
    
    // TODO   
    //thread_list[(long)thread] = 1;
    
    
    //original_pthread_mutex_lock(&global_lock);

    chess_schedule();
    //fprintf(stderr, "exiting2");
    return ret;
}

extern "C"
int pthread_join(pthread_t joinee, void **retval)
{
	//printf("Join Called\n");
    initialize_original_functions();
    long cur_t = (long)pthread_self();
    int other = 0;
    int me = 0;
    if(threads[0] == cur_t)
    {
        gl_holder = threads[1];
        other = 1;
        me = 0;
    }else
    {
        gl_holder = threads[0];
        other = 0;
        me =1;
    }
    original_pthread_mutex_unlock(&global_lock);

    //printf("%ld: waiting for: %ld to join\n",cur_t,(long)joinee);
    while(status[other] != 3)
    {
		//busy wait
    }
    //printf("%ld: joined with %ld\n",cur_t, (long)joinee);
    status[me] = 1;
    original_pthread_mutex_lock(&global_lock);
    FILE* sync = fopen("syncs", "w");
    fprintf(sync, "%d\n", sync_count);
    fclose(sync);
    return original_pthread_join(joinee, retval);
}

extern "C"
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    chess_schedule();
    initialize_original_functions();
    int rc = 0;
    long cur_t = (long)pthread_self();
    
    if(0 != pthread_mutex_trylock(mutex))
    {
        if(threads[0] == cur_t)
        {
            gl_holder = threads[1];
            status[0] = 2;
        }else
        {
            gl_holder = threads[0];
            status[1] = 2;
        }
    	original_pthread_mutex_unlock(&global_lock);
        
    	while(gl_holder != cur_t)
        {
            //busy wait
        }
    	original_pthread_mutex_lock(&global_lock);
    	rc = original_pthread_mutex_lock(mutex);
    }
    else
    {
	    //rc = original_pthread_mutex_lock(mutex);	    
    }
    return rc;
}

extern "C"
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{

    initialize_original_functions();
    int rc = -1; 
    rc = original_pthread_mutex_unlock(mutex);

    chess_schedule();
    return rc;
}

extern "C"
int sched_yield(void)
{
    if(status[0] == 3 || status[1] == 3)
        return 0;
    long cur_t = (long)pthread_self();

    if(threads[0] == cur_t)
    {
        gl_holder = threads[1];
        status[0] = 1;
    }else
    {
        gl_holder = threads[0];
        status[1] = 1;
    }
    //fprintf(stderr, "%ld %ld \n", threads[0], threads[1]);
    original_pthread_mutex_unlock(&global_lock);
    while(gl_holder != cur_t)
    {
		//busy wait
    }
    original_pthread_mutex_lock(&global_lock);
    //fprintf(stderr, "here");
    return 0;
}

static
void initialize_original_functions()
{
    static bool initialized = false;
    if (!initialized) {
        initialized = true;

        original_pthread_create =
            (int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*))dlsym(RTLD_NEXT, "pthread_create");
        original_pthread_join = 
            (int (*)(pthread_t, void**))dlsym(RTLD_NEXT, "pthread_join");
        original_pthread_mutex_lock =
            (int (*)(pthread_mutex_t*))dlsym(RTLD_NEXT, "pthread_mutex_lock");
        original_pthread_mutex_unlock =
            (int (*)(pthread_mutex_t*))dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    }
}

extern "C"
void chess_schedule(void)
{
    static int interrupt = -1;
    if(sync_count == 0)
    {
        syncfile = fopen("syncfile", "r");
        if(syncfile != NULL)
        {
            fscanf(syncfile, "%d", &interrupt);
            fclose(syncfile);
        }
    }
    if(interrupt == sync_count)
    {
        sched_yield();
    }
    sync_count++;
}

