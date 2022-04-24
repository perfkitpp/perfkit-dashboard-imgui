//
// Created by ki608 on 2022-03-20.
//

#include "TimePlot.hpp"

auto TimePlotWindowManager::CreateSlot(string name) -> TimePlotSlotProxy
{
    VerifyMainThread();

    auto data = make_shared<TimePlot::SlotData>();

    TimePlotSlotProxy proxy;
    proxy._keyAnchor = make_shared<nullptr_t>();
    proxy._bodyWeak = data;

    data->name = std::move(name);
    data->onlineAnchor = proxy._keyAnchor;

    return TimePlotSlotProxy();
}
