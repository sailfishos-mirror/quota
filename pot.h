#ifndef _POT_H
#define _POT_H

#define __GETTEXT__
/***************************************************************************
 * if you want to turn off gettext without changing sources 
 * undefine __GETTEXT__
 ***************************************************************************/

#ifdef __GETTEXT__

#include <libintl.h>

#define _(x)	gettext((x))

void gettexton(void);

#else

#define _(x) 	(x)

#endif

#endif
