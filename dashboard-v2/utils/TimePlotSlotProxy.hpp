//
// Created by ki608 on 2022-04-24.
//

#pragma once
#include <chrono>

using std::chrono::microseconds;

class TimePlotWindowManager;

namespace TimePlot {
class SlotData;
}

class TimePlotSlotProxy
{
    friend TimePlotWindowManager;

   private:
    shared_ptr<TimePlot::SlotData> _body;

   public:
    void Commit(double);
    void Expire();

    void FocusMe() {}
    explicit operator bool() const noexcept;
};
