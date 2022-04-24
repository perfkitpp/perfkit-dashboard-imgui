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
    shared_ptr<void> _keyAnchor;
    weak_ptr<TimePlot::SlotData> _bodyWeak;

   public:
    void Commit(double);
    void Expire() { _keyAnchor.reset(), _bodyWeak.reset(); }
    bool IsOnline() const noexcept { return not _bodyWeak.expired(); }
};
