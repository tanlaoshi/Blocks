/*
 * Blocks FreeType: force our ftoption/ftmodule/ftstdlib before library headers.
 */
#ifndef FT2BUILD_H_
#define FT2BUILD_H_

#define FT_CONFIG_OPTIONS_H           <ftoption.h>
#define FT_CONFIG_MODULES_H           <ftmodule.h>
#define FT_CONFIG_STANDARD_LIBRARY_H  <ftstdlib.h>

#include <freetype/config/ftheader.h>

#endif
