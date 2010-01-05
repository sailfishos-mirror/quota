#ifndef POT_H
#define POT_H

#ifdef USE_GETTEXT

#include <libintl.h>

#define _(x)	gettext((x))

#else

#define _(x) 	(x)

#endif

void gettexton(void);

#endif
