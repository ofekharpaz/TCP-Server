#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>        
#include <unistd.h>        	          // for read/write/close
#include <sys/types.h>     	          /* standard system types        */
#include <netinet/in.h>    	          /* Internet address structures */
#include <sys/socket.h>   	          /* socket interface functions  */
#include <netdb.h>         	          /* host to IP resolution            */
#include <signal.h>
#define maxSize 4000
threadpool* create_threadpool(int num_threads_in_pool)
{
    if(num_threads_in_pool > MAXT_IN_POOL || num_threads_in_pool < 0){
		fprintf(stderr , "Illegal num of threads\n");
		return NULL;
	}
	
	//Allocate threadpool memory	
	threadpool* result = (threadpool*)malloc(sizeof(threadpool));
	if(result == NULL){
		perror("malloc"); //function that print if there are error in the function
		return NULL;
	}
	
	//Initialize threadpool properties
	result->num_threads = num_threads_in_pool;

	//Initialize job queue for the beginning there are no job in the queue
	result->qhead = NULL;
	result->qtail = NULL;
	result->qsize = 0;
	
	//Initialize locks and conditions
	if(pthread_mutex_init(&result->qlock, NULL) != 0)
	{
		perror("pthread_mutex_init");
		free(result);
		return NULL;
	}

	if(pthread_cond_init(&result->q_not_empty, NULL) != 0)
	{
		perror("pthread_mutex_init");
		free(result);
		return NULL;
	}

	if(pthread_cond_init(&result->q_empty, NULL) != 0)
	{
		perror("pthread_mutex_init");
		free(result);
		return NULL;
	}
	
	result->shutdown = 0;			//TRUE if the pool is in distruction process     
	result->dont_accept = 0;		//TRUE if destroy function has begun
	
	//Initialize threads array
	result->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads_in_pool);
	if(result->threads == NULL)
	{
		perror("malloc");
		free(result);
		return NULL;
	}
	
	int i;
	for(i = 0; i < num_threads_in_pool; i++)
	{	// we create each thred . when he will run he will go to the "do_work" func.
		if(pthread_create(&result->threads[i], NULL ,do_work, (void*)result) != 0)
		{ //if not working, distroy the alocated memory.
			perror("pthread_create");
			free(result->threads);
			free(result);
			return NULL;
		}
	}

	return result;
}
void* do_work(void* args)
{
    threadpool* arg=(threadpool*)args;
   
    while(1)
    {
        if(arg->shutdown==1) // check if destruction has commenced
        {  
            pthread_mutex_unlock(&arg->qlock); 
            return NULL; // exits the thread
        }
        pthread_mutex_lock(&arg->qlock);
        if(arg->shutdown==1) // check if destruction has commenced
        {  
            pthread_mutex_unlock(&arg->qlock); 
            return NULL; // exits the thread
        }

        while(arg->qsize==0)
        {
            pthread_cond_wait(&arg->q_not_empty, &arg->qlock);
            if(arg->shutdown==1) // check if destruction has commenced
            {  
            
                pthread_mutex_unlock(&arg->qlock); 
                return NULL; // exits the thread
            }
        }

    

        
        //using shared variables, we dont want 2 threads to take the same tasks so we use mutex
        
         
    
        //take the task
        work_t* temp=arg->qhead;
    
       
        arg->qsize--;

        if(arg->qsize==0)
        {
            arg->qtail = NULL;
            arg->qhead = NULL;
			
			if (arg->dont_accept==1) //signal 
				pthread_cond_signal(&(arg->q_empty));
        }
        else
             arg->qhead=arg->qhead->next;
        
        /*
        if(arg->dont_accept==1 && arg->qsize==0) // check if destruction has commenced 
        {
             temp->routine(temp->arg);
            //free(temp->arg);
            free(temp);
            printf("all finished, wait for suicide\n");
            pthread_mutex_unlock(&arg->qlock); 
            pthread_cond_signal(&arg->q_empty);
            return NULL; // exits the thread 
            
        }
        */
        
       
        pthread_mutex_unlock(&arg->qlock);
        //execute and free the task
        temp->routine(temp->arg);
            free(temp->arg);
            free(temp);
       
       

        

        
    }

    return NULL;

}
void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg)
{
    //if we cant accept request then we dont send it, return to main
    if(from_me->dont_accept==1)
        return;

    //create the work_t and enter its fields
    work_t* w = (work_t*)malloc(sizeof(work_t));
    if(w==NULL)
    {
        perror("malloc failed");
        return;
    }
    memset(w, '\0', sizeof(work_t));

    char* str=(char*)arg;
   
    w->next=NULL;
    w->routine=dispatch_to_here;


    w->arg=(char*)malloc(sizeof(char)*maxSize);
    if(w->arg==NULL)
    {
        perror("malloc failed");
        return;
    }
    memset(w->arg, '\0', sizeof(char)*maxSize);
    strcpy(w->arg, str);
//  this is a critical code part, so we use mutex
    //insert the node into the queue
    
    pthread_mutex_lock(&from_me->qlock);
    if(from_me->dont_accept==1) //check if dont accept changed again
    {
        free(w);
		return;
    } 

    if(from_me->qsize!=0)
    {
        from_me->qtail->next=w;
        from_me->qtail=from_me->qtail->next;
        
    }
    else
    {
       from_me->qtail=w;
       from_me->qhead=w;
    }
    from_me->qsize++;
    
   
    //end of critical path
    pthread_cond_signal(&from_me->q_not_empty);

    pthread_mutex_unlock(&from_me->qlock);
    
     
}
void destroy_threadpool(threadpool* destroyme)
{
   
   //critical section
	pthread_mutex_lock(&(destroyme->qlock));
	
	destroyme->dont_accept = 1;
	
	while (destroyme->qsize!=0)
		pthread_cond_wait(&(destroyme->q_empty),&(destroyme->qlock));
	
	destroyme->shutdown = 1;
	
	pthread_cond_broadcast(&(destroyme->q_not_empty)); //tell threads to suicide
	
	
	pthread_mutex_unlock(&(destroyme->qlock)); //end of critical section
	
	//join the threads
    void *retval;
	for (int i = 0; i < destroyme->num_threads; i++)
		pthread_join(destroyme->threads[i], &retval);
	
	//free the mutex, conditions and the object itself
	pthread_mutex_destroy(&(destroyme->qlock));
	
	pthread_cond_destroy(&(destroyme->q_not_empty));
    pthread_cond_destroy(&(destroyme->q_empty));


	free(destroyme->threads);
	free(destroyme);
}