#include "bundle.h"

#ifdef __APPLE__

#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Resolve <exe>/../Resources/assets once. Returns NULL unless the executable
 * really lives in a .../Contents/MacOS/ directory (i.e. an app bundle), which
 * keeps every non-bundle/dev invocation on the old code path.
 */
const char* bundle_assets_root(void) {
  static char s_root[PATH_MAX];
  static int s_done = 0;
  if (s_done)
    return s_root[0] ? s_root : NULL;
  s_done = 1;

  char exe[PATH_MAX];
  uint32_t size = sizeof(exe);
  if (_NSGetExecutablePath(exe, &size) != 0)
    return NULL;

  char exe_full[PATH_MAX];
  if (!realpath(exe, exe_full))
    return NULL;

  /* exe_full = .../island.app/Contents/MacOS/island. Only treat it as a
   * bundle if that marker is present; cut it off and append the Resources
   * path relative to the .app root. */
  char* macos = strstr(exe_full, "/Contents/MacOS/");
  if (!macos)
    return NULL;
  *macos = '\0';
  snprintf(s_root, sizeof(s_root), "%s/Contents/Resources/assets", exe_full);

  struct stat st;
  if (stat(s_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
    s_root[0] = '\0';
    return NULL;
  }
  return s_root;
}

#else /* !__APPLE__ */

const char* bundle_assets_root(void) { return NULL; }

#endif
