#ifndef _VECTOR_H_
#define _VECTOR_H_

#define VECTOR_DEFAULT_INIT_SIZE 32

#define VECTOR_GENERATE_TYPE_NAME(Type, Name, name)                            \
  typedef struct Name {                                                        \
    int cap;                                                                   \
    int len;                                                                   \
    Type *items;                                                               \
  } Name;                                                                      \
  extern Name *make_##name(void);                                              \
  extern int name##_len(Name *vec);                                            \
  extern Type *name##_data(Name *vec);                                         \
  extern Type name##_get(Name *vec, int index);                                \
  extern bool name##_set(Name *vec, int index, Type item);                     \
  extern void name##_append(Name *vec, Type item);                             \
  extern void name##_delete(Name *vec, int index);                             \
  extern void free_##name(Name *vec);

#define VECTOR_GENERATE_TYPE_NAME_IMPL(Type, Name, name)                       \
  static void name##_init(Name *vec) {                                         \
    vec->cap = VECTOR_DEFAULT_INIT_SIZE;                                       \
    vec->len = 0;                                                              \
    vec->items = (Type *)malloc(VECTOR_DEFAULT_INIT_SIZE * sizeof(Type));      \
  }                                                                            \
  Name *make_##name(void) {                                                    \
    Name *vec = (Name *)malloc(sizeof(Name));                                  \
    name##_init(vec);                                                          \
    return vec;                                                                \
  }                                                                            \
  int name##_len(Name *vec) { return vec->len; }                               \
  Type *name##_data(Name *vec) { return vec->items; }                          \
  Type name##_get(Name *vec, int index) {                                      \
    assert(index >= 0 && index < vec->len);                                    \
    return vec->items[index];                                                  \
  }                                                                            \
  bool name##_set(Name *vec, int index, Type item) {                           \
    if (index < 0 || index >= vec->len)                                        \
      return false;                                                            \
    vec->items[index] = item;                                                  \
    return true;                                                               \
  }                                                                            \
  static void name##_resize(Name *vec, int new_cap) {                          \
    Type *items = (Type *)realloc(vec->items, new_cap * sizeof(Type));         \
    if (items) {                                                               \
      vec->items = items;                                                      \
      vec->cap = new_cap;                                                      \
    }                                                                          \
  }                                                                            \
  void name##_append(Name *vec, Type item) {                                   \
    if (vec->len == vec->cap)                                                  \
      name##_resize(vec, vec->cap * 2);                                        \
    vec->items[vec->len++] = item;                                             \
  }                                                                            \
  void name##_delete(Name *vec, int index) {                                   \
    if (index < 0 || index > vec->len)                                         \
      return;                                                                  \
    for (int i = index; i < vec->len; ++i) {                                   \
      if (i != vec->len - 1)                                                   \
        vec->items[i] = vec->items[i + 1];                                     \
    }                                                                          \
    vec->len -= 1;                                                             \
    if (vec->len > 0 && vec->cap > VECTOR_DEFAULT_INIT_SIZE &&                 \
        vec->len == vec->cap / 4)                                              \
      name##_resize(vec, vec->cap / 2);                                        \
  }                                                                            \
  void free_##name(Name *vec) {                                                \
    vec->len = 0;                                                              \
    vec->cap = 0;                                                              \
    free(vec->items);                                                          \
    free(vec);                                                                 \
  }

#endif /* _VECTOR_H_ */
