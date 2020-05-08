#ifndef CONDUIT_H
#define CONDUIT_H

#include <Windows.h>
#include <glib.h>

BOOL localSocketServiceInit(guint16 port);
void localSocketServiceClose(void);

void conduitStartListen(void);
void conduitOpenRemotePort(void);
void conduitCleanUp(void);

#endif // CONDUIT_H
