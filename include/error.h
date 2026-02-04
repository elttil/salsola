#ifndef ERROR_H
#define ERROR_H
enum {
  ERROR_SUCCESS = 1,
  ERROR_NO_MEMORY,
  ERROR_NO_FILE,
  ERROR_INVALID_FORMAT,
  ERROR_INVALID_USER_MEMORY,
  ERROR_INVALID_FD,
  ERROR_INVALID_WHENCE,
  ERROR_FD_HAS_NO_WRITE,
  ERROR_FD_HAS_NO_READ,
  ERROR_FD_HAS_NO_GETDENT,
  ERROR_FD_HAS_NO_LSEEK,
  ERROR_FD_HAS_NO_MMAP,
  ERROR_FD_HAS_NO_KPOLL,
  ERROR_WRITE_EXCEEDS_BOUNDS, // EFBIG
  ERROR_READ_EXCEEDS_BOUNDS,  // EFBIG
  ERROR_MMAP_NOT_SUPPORTED,
  ERROR_MMAP_INVALID_FLAGS, // EINVAL
  ERROR_MMAP_INVALID_MAP,
  ERROR_READ_WOULD_BLOCK,
  ERROR_WRITE_WOULD_BLOCK,
  ERROR_TASK_NOT_FOUND,
  ERROR_NOT_A_PROCESS,
  ERROR_BUFFER_TOO_SMALL,
  ERROR_NOT_A_DIRECTORY,
  ERROR_FCNTL_INVALID_FLAGS,
  ERROR_EXHAUSTED,
  ERROR_INVALID_ELF_HEADER,
  ERROR_GENERIC_TODO,
};

#endif // ERROR_H

#define WARN_UNUSED __attribute__((warn_unused_result))

typedef int err_t;

#define UNUSED(expr)                                                           \
  if (expr) {                                                                  \
  }

#define TRY(expr)                                                              \
  {                                                                            \
    err_t macro_error;                                                         \
    if (ERROR_SUCCESS != (macro_error = (expr))) {                             \
      return macro_error;                                                      \
    }                                                                          \
  }

#define TRY_COND(expr, var, label)                                             \
  {                                                                            \
    err_t macro_error;                                                         \
    if (ERROR_SUCCESS != (macro_error = (expr))) {                             \
      var = macro_error;                                                       \
      goto label;                                                              \
    }                                                                          \
  }

#define ASSIGN_PTR(ptr, value)                                                 \
  if (ptr) {                                                                   \
    *(ptr) = (value);                                                          \
  } else {                                                                     \
    (void)(value);                                                             \
  }
