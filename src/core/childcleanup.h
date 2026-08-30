#ifndef CHILDCLEANUP_H
#define CHILDCLEANUP_H

#include <QtGlobal>
#include <QString>

namespace ChildCleanup {

void attachLlamaChild(qint64 pid);
void detachLlamaChild(qint64 pid);
QString reapStaleLlamaOnPort(int port);
QString reapForeignLlamaServers();

}

#endif // CHILDCLEANUP_H
