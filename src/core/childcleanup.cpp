#include "childcleanup.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#endif

#include <QByteArray>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

#ifdef Q_OS_WIN

namespace {

QSet<DWORD> &protectedPids()
{
    static QSet<DWORD> pids;
    return pids;
}

bool isProtected(DWORD pid)
{
    return pid != 0 && protectedPids().contains(pid);
}

bool isLlamaImageName(const QString &name)
{
    const QString n = QFileInfo(name).fileName().toLower();
    return n == QLatin1String("llama-server.exe")
        || n == QLatin1String("llama.exe");
}

QString processImage(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    QString out;
    if (QueryFullProcessImageNameW(h, 0, buf, &n))
        out = QString::fromWCharArray(buf);
    CloseHandle(h);
    return out;
}

HANDLE jobHandle()
{
    static HANDLE job = nullptr;
    if (job)
        return job;
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    ZeroMemory(&info, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(job);
        job = nullptr;
    }
    return job;
}

DWORD listenerPidOnPort(int port)
{
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (size == 0)
        return 0;
    QByteArray buf;
    buf.resize(int(size));
    auto *table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buf.data());
    if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR)
        return 0;
    const DWORD want = htons(u_short(port));
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID &row = table->table[i];
        if (row.dwState == MIB_TCP_STATE_LISTEN && row.dwLocalPort == want && row.dwOwningPid != 0)
            return row.dwOwningPid;
    }
    return 0;
}

QString terminateLlama(DWORD pid, const QString &why)
{
    if (pid == 0 || pid == GetCurrentProcessId() || isProtected(pid))
        return {};
    const QString image = processImage(pid);
    const QString name = image.isEmpty()
        ? QStringLiteral("llama-server.exe")
        : QFileInfo(image).fileName();
    if (!image.isEmpty() && !isLlamaImageName(image))
        return {};
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h)
        return QStringLiteral("Не удалось завершить %1 (pid %2): %3.")
            .arg(name)
            .arg(pid)
            .arg(why);
    const BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    if (!ok)
        return QStringLiteral("TerminateProcess не сработал для %1 (pid %2).").arg(name).arg(pid);
    return QStringLiteral("Снят чужой %1 (pid %2) — %3.")
        .arg(name)
        .arg(pid)
        .arg(why);
}

} // namespace

void ChildCleanup::attachLlamaChild(qint64 pid)
{
    if (pid <= 0)
        return;
    protectedPids().insert(DWORD(pid));
    HANDLE job = jobHandle();
    if (!job)
        return;
    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!proc)
        return;
    AssignProcessToJobObject(job, proc);
    CloseHandle(proc);
}

void ChildCleanup::detachLlamaChild(qint64 pid)
{
    if (pid <= 0)
        return;
    protectedPids().remove(DWORD(pid));
}

QString ChildCleanup::reapStaleLlamaOnPort(int port)
{
    const DWORD pid = listenerPidOnPort(port);
    const QString msg = terminateLlama(pid, QStringLiteral("держал порт %1").arg(port));
    if (!msg.isEmpty())
        Sleep(400);
    return msg;
}

QString ChildCleanup::reapForeignLlamaServers()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    QStringList notes;
    if (Process32FirstW(snap, &pe)) {
        do {
            const QString name = QString::fromWCharArray(pe.szExeFile);
            if (!isLlamaImageName(name))
                continue;
            const QString msg = terminateLlama(pe.th32ProcessID,
                                               QStringLiteral("остался после прошлого запуска"));
            if (!msg.isEmpty())
                notes << msg;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!notes.isEmpty())
        Sleep(600);
    return notes.join(QLatin1Char('\n'));
}

#else

void ChildCleanup::attachLlamaChild(qint64)
{
}

void ChildCleanup::detachLlamaChild(qint64)
{
}

QString ChildCleanup::reapStaleLlamaOnPort(int)
{
    return {};
}

QString ChildCleanup::reapForeignLlamaServers()
{
    return {};
}

#endif
