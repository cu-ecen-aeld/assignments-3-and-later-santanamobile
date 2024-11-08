#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* thread_func_args = (struct thread_data*) thread_param;

    // Wait a bit before locking the mutex
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // Lock the mutex
    pthread_mutex_lock(thread_func_args->mutex);

    // Hold the lock for a while
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // Unlock the mutex
    pthread_mutex_unlock(thread_func_args->mutex);

    // Mark the operation as complete
    thread_func_args->thread_complete_success = true;
    return thread_func_args;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms) {
    struct thread_data* data = (struct thread_data*) malloc(sizeof(struct thread_data));
    if (data == NULL) {
        ERROR_LOG("Memory allocation failed\n");
        return false;
    }

    // Setup thread data
    data->thread_complete_success = false;
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;

    // Create the thread
    int result = pthread_create(thread, NULL, threadfunc, data);
    if (result != 0) {
        ERROR_LOG("Thread creation failed\n");
        free(data);
        return false;
    }

    DEBUG_LOG("Thread started\n");
    return true;
}
