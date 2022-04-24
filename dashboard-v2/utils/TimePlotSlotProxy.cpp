//
// Created by ki608 on 2022-04-24.
//

#include "TimePlotSlotProxy.hpp"

#include "widgets/TimePlot.hpp"

void TimePlotSlotProxy::Commit(double d)
{
    VerifyMainThread();

    if (_body->bMarkDestroied)
    {
        _body.reset();
        return;
    }

    _body->pointsPendingUploaded.push_back({steady_clock::now(), d});
}

void TimePlotSlotProxy::Expire()
{
    VerifyMainThread();

    _body->bMarkDestroied = true;
    _body.reset();
}

TimePlotSlotProxy::operator bool() const noexcept
{
    VerifyMainThread();

    return _body && not _body->bMarkDestroied;
}
