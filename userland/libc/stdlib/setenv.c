#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct env {
  char *name;
  char *value;
  struct env *next;
};

static size_t num_envs = 0;
static struct env *env_head = NULL;

struct env *internal_getenv(const char *name) {
  struct env *p = env_head;
  for (; p; p = p->next) {
    if (0 == strcmp(p->name, name)) {
      return p;
    }
  }
  return NULL;
}

void __getenv_array_free(struct sv *env_array, size_t length) {
	(void)env_array;
	(void)length;
	// TODO:
}

struct sv *__getenv_array(size_t *length) {
  struct sv *array = malloc(sizeof(struct sv )*num_envs);
  if(!array) return NULL;

  struct env *p = env_head;
  for (size_t i = 0; p; p = p->next,i++) {
    assert(i < num_envs);
	
    size_t l = strlen(p->name)+1+strlen(p->value)+1;
	char *str = malloc(l);
    assert(str); // TODO:
	int rc = snprintf(str, l, "%s=%s", p->name, p->value);

	array[i] = sv_init(str, rc);
  }
  if(length) *length = num_envs;
  return array;
}

int setenv(const char *name, const char *value, int overwrite) {
  if (NULL == name) {
    errno = -EINVAL;
    return -1;
  }

  int name_length = strlen(name);
  if (0 == name_length) {
    errno = -EINVAL;
    return -1;
  }
  int value_length = strlen(value);

  struct env *p = internal_getenv(name);
  if (p) {
    if (!overwrite) {
      return 0;
    }
    char *new_ptr = realloc(p->value, value_length + 1);
    if (!new_ptr) {
      errno = -ENOMEM;
      return -1;
    }
    p->value = new_ptr;
    strcpy(p->value, value);
    return 0;
  }

  struct env *new_env = malloc(sizeof(struct env));
  if (!new_env) {
    return -ENOMEM;
  }
  new_env->name = malloc(name_length + 1);
  if (!new_env->name) {
    free(new_env);
    return -ENOMEM;
  }
  new_env->value = malloc(value_length + 1);
  if (!new_env->value) {
    free(new_env->name);
    free(new_env);
    return -ENOMEM;
  }
  strcpy(new_env->name, name);
  strcpy(new_env->value, value);
  new_env->next = env_head;
  env_head = new_env;
  num_envs++;
  return 0;
}
