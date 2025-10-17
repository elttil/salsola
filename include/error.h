#define ERROR_SUCCESS 1
#define ERROR_NO_MEMORY 2
#define ERROR_NO_FILE 3
#define ERROR_INVALID_FORMAT 4
#define ERROR_INVALID_USER_MEMORY 4
#define ERROR_INVALID_FD 5
#define ERROR_INVALID_WHENCE 6
#define ERROR_FD_HAS_NO_WRITE 7
#define ERROR_FD_HAS_NO_READ 8
#define ERROR_FD_HAS_NO_LSEEK 9

typedef int err_t;

#define TRY(expr)                                                              \
  {                                                                            \
    err_t macro_error;                                                         \
    if (ERROR_SUCCESS != (macro_error = (expr))) {                             \
      return macro_error;                                                      \
    }                                                                          \
  }

#define ASSIGN_PTR(ptr, value)                                                 \
  if (ptr) {                                                                   \
    *(ptr) = (value);                                                          \
  }
