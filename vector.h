#include <stdlib.h>
#include <string.h>

#ifndef VECTOR_NAME
#error "Must declare VECTOR_NAME"
#endif

#ifndef VECTOR_TYPE
#error "Must declare VECTOR_TYPE"
#endif

#define VECTOR_CONCAT(x, y) x ## _ ## y
#define VECTOR_1(x, y) VECTOR_CONCAT(x, y)
#define VECTOR_(x) VECTOR_1(vec, VECTOR_1(VECTOR_NAME,x))
#define VECTOR VECTOR_(t)

typedef struct {
  VECTOR_TYPE *arr;
  size_t size;
  size_t alloc;
} VECTOR_(struct);

typedef VECTOR_(struct) VECTOR[1];

static inline void VECTOR_(init)(VECTOR vec) {
  vec[0].alloc = 8;
  vec[0].arr = calloc(vec->alloc, sizeof(VECTOR_TYPE));
  vec[0].size = 0;
}

static inline void VECTOR_(append)(VECTOR vec, VECTOR_TYPE elt) {
  if (vec[0].size == vec[0].alloc) {
    vec[0].alloc += (vec[0].alloc >> 1);
    vec[0].arr = realloc(vec[0].arr, vec[0].alloc * sizeof(VECTOR_TYPE));
  }
  vec[0].arr[vec[0].size++] = elt;
}

static inline VECTOR_TYPE VECTOR_(get)(VECTOR vec, int i) {
  return vec[0].arr[i];
}

static inline VECTOR_TYPE VECTOR_(pop)(VECTOR vec) {
  return vec[0].arr[vec[0].size--];
}

static inline void VECTOR_(swap)(VECTOR vec, int i, int j) {
  VECTOR_TYPE v = vec[0].arr[i];
  vec[0].arr[i] = vec[0].arr[j];
  vec[0].arr[j] = v;
}

static inline void VECTOR_(del)(VECTOR vec, int i) {
  if (i == vec[0].size) {
    VECTOR_(pop)(vec);
  } else {
    memmove(&(vec[0].arr[i]), &(vec[0].arr[i + 1]), sizeof(VECTOR_TYPE) * vec[0].size - i - 1);
    vec[0].size--;
  }
}

static inline size_t VECTOR_(size)(VECTOR vec) {
  return vec[0].size;
}

static inline void VECTOR_(shuf)(VECTOR vec) {
  for (int i = 0; i < VECTOR_(size)(vec); i++)
    VECTOR_(swap)(vec, i, rand() % (i+1));
}

#undef VECTOR_1
#undef VECTOR_
#undef VECTOR_NAME
#undef VECTOR_TYPE
#undef VECTOR
