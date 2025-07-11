#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

static size_t cfg_seed;
static size_t freq = 9257;
static size_t next_rand;

__attribute__((constructor)) void cfg_init()
{
  cfg_seed = 42;
  next_rand = cfg_seed * 1103515245 + 12345;
}

static int cfg_rand()
{
  size_t ret = next_rand;
  next_rand = next_rand * 1103515245 + 12345;
  return ret % 32768;
}

static bool cfg_return_null()
{
  if (cfg_rand() % freq == 0) {
    /** Inject no memory error */
    errno = ENOMEM;
    return true;
  }
  return false;
}

/** Possibly null malloc */
void *cfg_malloc(size_t size)
{
  return cfg_return_null() ? NULL : malloc(size);
}

void *cfg_calloc(size_t nmemb, size_t size)
{
  return cfg_return_null() ? NULL : calloc(nmemb, size);
}

void *cfg_realloc(void *ptr, size_t size)
{
  return cfg_return_null() ? NULL : realloc(ptr, size);
}

void *cfg_reallocarray(void *ptr, size_t nmemb, size_t size)
{
  return cfg_return_null() ? NULL : reallocarray(ptr, nmemb, size);
}
