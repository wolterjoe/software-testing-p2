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

std::map< long, int > thread_list; //threadID, state of thread
std::map< long, int >::iterator t_itr;

std::map< pthread_mutex_t*, std::queue<long> > mutex_queue_list; //lock, threadID of lock owner
std::map< pthread_mutex_t*, std::queue<long> >::iterator l_itr;

std::map< pthread_mutex_t*, long > mutex_list;
std::map< pthread_mutex_t*, long >::iterator m_itr;

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
    thread_list[(long)pthread_self()] = 1;
    original_pthread_mutex_lock(&global_lock);
    void* rc = thread_arg.start_routine(thread_arg.arg);
    thread_list[(long)pthread_self()] = 3;
    if(thread_list.size() < 2)
    {
        FILE* sync = fopen("syncs", "w");
        fprintf(sync, "%d\n", sync_count);
        fclose(sync);
    }
    
    //original_pthread_mutex_unlock(&global_lock);
    original_pthread_mutex_unlock(&global_lock);

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
    	//printf("firstrun self: %ld\n",(long)pthread_self());
    	//printf("firstrun thread: %ld\n",(long)thread);
    	thread_list[gl_holder] = 1;
    	
    }
    int ret = original_pthread_create(thread, attr, thread_main, thread_arg);  
    
    // TODO   
    //thread_list[(long)thread] = 1;
    
    
    //original_pthread_mutex_lock(&global_lock);

    chess_schedule();
    return ret;
}

extern "C"
int pthread_join(pthread_t joinee, void **retval)
{
	//printf("Join Called\n");
    initialize_original_functions();
    long cur_t = (long)pthread_self();
    original_pthread_mutex_unlock(&global_lock);
    for(t_itr = thread_list.begin(); t_itr != thread_list.end(); t_itr++)
    {
    	if(t_itr->first != cur_t && t_itr->second == 1)
    	{
    		gl_holder = t_itr->first;
    		break;
    	}
    }
    //printf("%ld: waiting for: %ld to join\n",cur_t,(long)joinee);
    while(thread_list[(long)joinee] != 3)
    {
		//busy wait
    }
    //printf("%ld: joined with %ld\n",cur_t, (long)joinee);
    thread_list[cur_t] = 1;
    original_pthread_mutex_lock(&global_lock);
    return original_pthread_join(joinee, retval);
}

extern "C"
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    chess_schedule();
    initialize_original_functions();
    int rc;
    long cur_t = (long)pthread_self();
    std::queue<long> &mutex_queue = mutex_queue_list[mutex];
    //printf("Mutex Queue List contains %d queues\n",mutex_queue_list.size());
    
    //printf("mutex lock front of queue before push: %ld : Size: %d\n", mutex_queue.front(),mutex_queue.size());
   // printf("mutex lock pushing: %ld\n", cur_t);
    mutex_queue.push(cur_t);
    //printf("mutex lock front of queue after push: %ld : Size: %d\n", mutex_queue.front(),mutex_queue.size());
    m_itr = mutex_list.find(mutex);
    if((mutex_queue.front() != cur_t) || (m_itr != mutex_list.end()))
    {
    	//printf("front mutex lock: %ld\n", mutex_queue.front());
    	//printf("test mutex queue list: %ld\n",mutex_queue.front()); 
    	original_pthread_mutex_unlock(&global_lock);
    	thread_list[cur_t] = 2;
    	for(t_itr = thread_list.begin(); t_itr != thread_list.end(); t_itr++)
    	{
    		//printf("%ld: lock: searching for thread: %ld\n", cur_t,t_itr->first);
    		if(t_itr->first != cur_t && t_itr->second == 1)
    		{
    			//printf("%ld: lock: switching to: %ld\n", cur_t,t_itr->first);
    			gl_holder = t_itr->first;
    			break;
    		}
    		else
    		{
    			//printf("%ld: lock: not switching\n", cur_t);
    			gl_holder = cur_t;
    		}
    	}
	   //printf("%ld: waiting for lock\n",cur_t);
    	m_itr = mutex_list.find(mutex);	
    	while(mutex_queue.front() != cur_t || (m_itr != mutex_list.end()))
    	{
    		m_itr = mutex_list.find(mutex);
    		//busy wait
    	}
    	//printf("%ld: done waiting for lock\n",cur_t);
    	mutex_list[mutex] = cur_t;
    	thread_list[cur_t] = 1;
    	
    	original_pthread_mutex_lock(&global_lock);
    	rc = original_pthread_mutex_lock(mutex);
    }
    else
    {
    	    //printf("%ld: didnt wait for lock\n", cur_t); 
    	 mutex_list[mutex] = cur_t;
	     rc = original_pthread_mutex_lock(mutex);	    
    }
    return rc;
}

extern "C"
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{

    initialize_original_functions();
    int rc = -1;
    long cur_t = (long)pthread_self();
    //original_pthread_mutex_unlock(&global_lock);
    std::queue<long> &mutex_queue = mutex_queue_list[mutex];
    //printf("%ld: unlocking\n", cur_t);
    if(mutex_list[mutex] == cur_t)
    {	    
	    rc = original_pthread_mutex_unlock(mutex);
	    mutex_queue.pop();   
	    gl_holder = cur_t;
	    mutex_list.erase(mutex);
	    //printf("%ld: unlocked\n", cur_t);
    }
    else
    {
    //printf("%ld: unlocking FAILED\n", cur_t);
    //printf("%ld: unlocking FAILED\n", cur_t);
    }
    //original_pthread_mutex_lock(&global_lock);
    chess_schedule();
    return rc;
}

extern "C"
int sched_yield(void)
{
    long cur_t = (long)pthread_self();
    original_pthread_mutex_unlock(&global_lock);
    //printf("%ld: yielding\n", cur_t);
    //printf("thread queue size: %d\n", thread_list.size());
    for(t_itr = thread_list.begin(); t_itr != thread_list.end(); t_itr++)
    {
    	    //printf("%ld: yield: searching for thread: %ld\n", cur_t,t_itr->first);
    	if(t_itr->first != cur_t && t_itr->second == 1)
    	{
    		//printf("%ld: yield: switching to: %ld\n", cur_t,t_itr->first);
    		gl_holder = t_itr->first;
    		break;
    	}
    }
    //original_pthread_mutex_lock(&global_lock);
    while(gl_holder != cur_t && thread_list.size() > 1)
    {
		//busy wait
    }
    original_pthread_mutex_lock(&global_lock);
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

