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
  ERROR_FD_HAS_NO_LSEEK,
  ERROR_FD_HAS_NO_MMAP,
  ERROR_WRITE_EXCEEDS_BOUNDS, // EFBIG
  ERROR_READ_EXCEEDS_BOUNDS,  // EFBIG
  ERROR_MMAP_NOT_SUPPORTED,
  ERROR_MMAP_INVALID_FLAGS, // EINVAL
};

#endif // ERROR_H
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
