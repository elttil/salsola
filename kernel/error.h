#define ERROR_SUCCESS 0
#define ERROR_NO_MEMORY 1
#define ERROR_NO_FILE 2
#define ERROR_INVALID_FORMAT 3

#define ASSIGN_ERR(err, value)                                                 \
  if (err) {                                                                   \
    *(err) = (value);                                                          \
  }
