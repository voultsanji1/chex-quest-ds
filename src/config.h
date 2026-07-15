/* Minimal config.h for the Nintendo DS (BlocksDS) build of Chex Quest DS.
 *
 * The upstream Chocolate Doom build generates this file with autoconf. The
 * NDS platform layer and the (unmodified) Doom engine only reference a couple
 * of the PACKAGE_* / VERSION macros at runtime; no feature-detection macros
 * (HAVE_*, SIZEOF_*) are actually used by the code that gets compiled for the
 * DS. Those optional features (FluidSynth, libsamplerate, libpng, ...) are
 * intentionally left undefined because the DS build does not link them.
 */

#ifndef CONFIG_H
#define CONFIG_H

#define PACKAGE_NAME "chexquest"
#define PACKAGE_TARNAME "chexquest"
#define PACKAGE_VERSION "0.2.0"
#define PACKAGE_STRING "Chex Quest DS 0.2.0"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""

#define VERSION "0.2.0"

/* Standard C headers are available on the DS toolchain. */
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

#endif /* CONFIG_H */
