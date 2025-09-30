#ifndef LOGGING_H
#define LOGGING_H

typedef enum log_level_e { ERROR = 0, WARNING, INFO, DEBUG } log_level_t;

extern log_level_t log_level;

#define LOG_ERROR(msg, ...)                                                    \
  do {                                                                         \
    if (log_level >= ERROR)                                                    \
      fprintf(stderr, "[ERROR] " msg "\n", ##__VA_ARGS__);                     \
  } while (0)

#define LOG_WARN(msg, ...)                                                     \
  do {                                                                         \
    if (log_level >= WARNING)                                                  \
      fprintf(stderr, "[WARN ] " msg "\n", ##__VA_ARGS__);                     \
  } while (0)

#define LOG_INFO(msg, ...)                                                     \
  do {                                                                         \
    if (log_level >= INFO)                                                     \
      fprintf(stdout, "[INFO ] " msg "\n", ##__VA_ARGS__);                     \
  } while (0)

#define LOG_DEBUG(msg, ...)                                                    \
  do {                                                                         \
    if (log_level >= DEBUG)                                                    \
      fprintf(stdout, "[DEBUG] " msg "\n", ##__VA_ARGS__);                     \
  } while (0)

#endif
