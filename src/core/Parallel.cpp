#include "Parallel.h"

namespace sc {

QThreadPool &fanoutPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        // Source fan-out mostly waits on the network.
        p->setMaxThreadCount(24);
        p->setExpiryTimeout(60000);
        return p;
    }();
    return *pool;
}

QThreadPool &batchPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        // Requests to different sites do not compete, so a few more than
        // Mihon's five; each site sets its own pace (parallelByKeyEach).
        p->setMaxThreadCount(8);
        p->setExpiryTimeout(60000);
        return p;
    }();
    return *pool;
}

} // namespace sc
