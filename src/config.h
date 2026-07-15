/* Minimal config.h for the Nintendo DS (BlocksDS) build of Chex Quest DS.
 *
 * Derived from cmake/config.h.cin. The optional feature libraries
 * (FluidSynth, libsamplerate, libpng) and mmap are not available on the
 * DS and are left undefined so the engine uses its fallback paths.
 */

#ifndef CONFIG_H
#define CONFIG_H

#define PACKAGE_NAME "chexquest"
#define PACKAGE_TARNAME "chexquest"
#define PACKAGE_VERSION "0.2.0"
#define PACKAGE_STRING "Chex Quest DS 0.2.0"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""
#define PROGRAM_PREFIX ""

#define VERSION "0.2.0"

/* Standard C headers available on the DS toolchain (newlib). */
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_DIRENT_H 1

/* newlib declares str[n]casecmp (in <strings.h>), so the engine's
 * Windows fallback (stricmp/strnicmp) must stay disabled. */
#define HAVE_DECL_STRCASECMP 1
#define HAVE_DECL_STRNCASECMP 1

/* Disabled optional features (absent on the DS): */
/* #undef HAVE_FLUIDSYNTH */
/* #undef HAVE_LIBSAMPLERATE */
/* #undef HAVE_LIBPNG */
/* #undef HAVE_MMAP */

#endif /* CONFIG_H */
