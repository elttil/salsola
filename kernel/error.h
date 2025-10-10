#define ERROR_SUCCESS 1
#define ERROR_NO_MEMORY 2
#define ERROR_NO_FILE 3
#define ERROR_INVALID_FORMAT 4

typedef int err_t;

#define ASSIGN_PTR(ptr, value) \
  if (ptr) {                                                                   \
    *(ptr) = (value);                                                          \
  }
