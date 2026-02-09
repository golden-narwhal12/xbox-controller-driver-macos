/*******************************************************************************
 * thread.h - Thread safety wrappers using pthread
 ******************************************************************************/

#ifndef THREAD_H
#define THREAD_H

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

/*******************************************************************************
 * Atomic Running Flag
 ******************************************************************************/
typedef struct {
    atomic_bool value;
} AtomicBool;

static inline void atomic_bool_init(AtomicBool *ab, bool initial) {
    atomic_init(&ab->value, initial);
}

static inline bool atomic_bool_get(AtomicBool *ab) {
    return atomic_load(&ab->value);
}

static inline void atomic_bool_set(AtomicBool *ab, bool val) {
    atomic_store(&ab->value, val);
}

/*******************************************************************************
 * Mutex Wrapper
 ******************************************************************************/
typedef struct {
    pthread_mutex_t mutex;
} Mutex;

static inline int mutex_init(Mutex *m) {
    return pthread_mutex_init(&m->mutex, NULL);
}

static inline void mutex_destroy(Mutex *m) {
    pthread_mutex_destroy(&m->mutex);
}

static inline void mutex_lock(Mutex *m) {
    pthread_mutex_lock(&m->mutex);
}

static inline void mutex_unlock(Mutex *m) {
    pthread_mutex_unlock(&m->mutex);
}

static inline int mutex_trylock(Mutex *m) {
    return pthread_mutex_trylock(&m->mutex);
}

/*******************************************************************************
 * Read-Write Lock Wrapper
 ******************************************************************************/
typedef struct {
    pthread_rwlock_t rwlock;
} RWLock;

static inline int rwlock_init(RWLock *rw) {
    return pthread_rwlock_init(&rw->rwlock, NULL);
}

static inline void rwlock_destroy(RWLock *rw) {
    pthread_rwlock_destroy(&rw->rwlock);
}

static inline void rwlock_read_lock(RWLock *rw) {
    pthread_rwlock_rdlock(&rw->rwlock);
}

static inline void rwlock_read_unlock(RWLock *rw) {
    pthread_rwlock_unlock(&rw->rwlock);
}

static inline void rwlock_write_lock(RWLock *rw) {
    pthread_rwlock_wrlock(&rw->rwlock);
}

static inline void rwlock_write_unlock(RWLock *rw) {
    pthread_rwlock_unlock(&rw->rwlock);
}

/*******************************************************************************
 * Condition Variable Wrapper
 ******************************************************************************/
typedef struct {
    pthread_cond_t cond;
} CondVar;

static inline int condvar_init(CondVar *cv) {
    return pthread_cond_init(&cv->cond, NULL);
}

static inline void condvar_destroy(CondVar *cv) {
    pthread_cond_destroy(&cv->cond);
}

static inline void condvar_wait(CondVar *cv, Mutex *m) {
    pthread_cond_wait(&cv->cond, &m->mutex);
}

static inline void condvar_signal(CondVar *cv) {
    pthread_cond_signal(&cv->cond);
}

static inline void condvar_broadcast(CondVar *cv) {
    pthread_cond_broadcast(&cv->cond);
}

#endif // THREAD_H
