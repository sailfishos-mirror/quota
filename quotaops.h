#ifndef _QUOTAOPS_H
#define _QUOTAOPS_H

#include "quotaio.h"

__BEGIN_DECLS extern struct dquot *getprivs __P((qid_t id, struct quota_handle ** handles));
extern int putprivs __P((struct dquot * qlist));
extern int editprivs __P((char *tmpfile));
extern int writeprivs __P((struct dquot * qlist, int outfd, char *name, int quotatype));
extern int readprivs __P((struct dquot * qlist, int infd));
extern int writetimes __P((struct quota_handle ** handles, int outfd));
extern int readtimes __P((struct quota_handle ** handles, int infd));
extern void freeprivs __P((struct dquot * qlist));

__END_DECLS
#endif /* _QUOTAOPS_H */
