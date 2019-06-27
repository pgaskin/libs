// value_waiter - v3 - public domain - by Patrick Gaskin

#include <stdbool.h>
#include <pthread.h>

#define VALUE_WAITER_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0}

/*
 * value_waiter_t lets a thread pass a value to another thread waiting
 * for it. Each time a value is sent, it replaces the previous one,
 * until the value is read or cleared. Reading blocks until a value is
 * present. This is completely thread-safe and each value is guaranteed
 * to be received at most once.
 */
typedef struct value_waiter_t {
    pthread_mutex_t mut;
    pthread_cond_t cond;
    int v;
} value_waiter_t;

/*
 * vw_init initializes the value waiter. The behaviour of the other vw
 * functions is undefined until this is called (or the value_waiter_t
 * is initialized using the initializer).
 */
static inline void vw_init(struct value_waiter_t* vw) {
    *vw = (value_waiter_t) VALUE_WAITER_INITIALIZER;
}

/*
 * vw_clear clears the stored value, and can be used to ignore all values
 * previously stored.
 */
static inline void vw_clear(struct value_waiter_t* vw) {
    vw->v = 0;
}

/*
 * vw_put stores a value, and can safely be called at the same time as
 * vw_get. Only the last value stored will be returned by vw_get (i.e.
 * the values will not queue, each replaces the previous one).
 */
static inline void vw_put(struct value_waiter_t* vw, int v) {
    pthread_mutex_lock(&vw->mut);
    vw->v = v;
    pthread_cond_signal(&vw->cond);
    pthread_mutex_unlock(&vw->mut);
}

/*
 * vw_get clears and returns the value stored if any, or else waits for
 * a value to be set and returns it. It is guaranteed only one thread
 * will recieve each value, and that the values will not queue up (i.e.
 * this consumes all pending values and returns the last one). vw_get
 * can be called by multiple threads, and they will behave as if called
 * sequentially (i.e. each one will need a new value put into it in
 * between calls). If wait is zero, vw_get will not wait for a value and
 * return zero if there is none available.
 */
static inline int vw_get(struct value_waiter_t* vw, bool wait) {
    pthread_mutex_lock(&vw->mut);
    int v = vw->v;
    while (wait && !(v = vw->v))
        pthread_cond_wait(&vw->cond, &vw->mut);
    vw_clear(vw);
    pthread_mutex_unlock(&vw->mut);
    return v;
}

/*
 * vw_has checks if there is a value available. Note that to avoid race
 * conditions, you should use vw_get instead.
 */
static inline bool vw_has(struct value_waiter_t* vw) {
    pthread_mutex_lock(&vw->mut);
    int v = vw->v;
    pthread_mutex_unlock(&vw->mut);
    return v != 0;
}
