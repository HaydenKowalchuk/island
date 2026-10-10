#include <miniaudio.h>
#include <stdio.h>

int main(void) {
  ma_uint32 ver_major = 0, ver_minor = 0, ver_patch = 0;
  ma_version(&ver_major, &ver_minor, &ver_patch);
  printf("miniaudio %u.%u.%u\n", ver_major, ver_minor, ver_patch);

  /* dev-0.12 has not bumped the MA_VERSION_* macros yet, this is expected --
   * the branch name is what matters, not the literal string. */
  if (ver_major != 0 || ver_minor != 11 || ver_patch != 26) {
    fprintf(stderr, "unexpected miniaudio version, wanted 0.11.26\n");
    return 1;
  }
  return 0;
}
