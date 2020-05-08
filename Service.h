#ifndef SERVICE_H
#define SERVICE_H

#include <Windows.h>
#include <glib.h>

#define SERVICE_NAME L"ConduitD"

void    quitSignaled (int sig);
BOOL    serviceInit(void);
void    serviceMainLoopExec(void);


#endif // SERVICE_H
