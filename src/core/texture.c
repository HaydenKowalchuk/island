#include "texture.h"

#include <string.h>

/*
 * Name -> handle cache. The backend owns the actual resource; core only
 * remembers the mapping so repeated content lookups stay cheap. Failures (no
 * loader, or a missing file) are not cached so the loader can be installed
 * later without a stale zero to trip over.
 */
#define TEXTURE_CACHE 64

typedef struct {
  char name[64];
  TextureId id;
} Entry;

static Entry s_cache[TEXTURE_CACHE];
static int s_cache_count = 0;
static TextureLoader s_loader = NULL;

void texture_set_loader(TextureLoader loader) {
  s_loader = loader;
}

TextureId texture_get(const char* name) {
  if (!name || !*name || !s_loader)
    return 0;

  for (int i = 0; i < s_cache_count; i++) {
    if (strcmp(s_cache[i].name, name) == 0)
      return s_cache[i].id;
  }

  TextureId id = s_loader(name);
  if (id == 0)
    return 0;

  if (s_cache_count < TEXTURE_CACHE) {
    strncpy(s_cache[s_cache_count].name, name, sizeof(s_cache[0].name) - 1);
    s_cache[s_cache_count].name[sizeof(s_cache[0].name) - 1] = '\0';
    s_cache[s_cache_count].id = id;
    s_cache_count++;
  }
  return id;
}
