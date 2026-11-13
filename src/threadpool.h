#ifndef threadpool_h_INCLUDED
#define threadpool_h_INCLUDED

#include <threads.h>

typedef struct {
    size_t n_threads;
    thrd_t* threads;
    
} threadpool_t;


#endif // threadpool_h_INCLUDED
