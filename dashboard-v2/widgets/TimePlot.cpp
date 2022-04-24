//
// Created by ki608 on 2022-03-20.
//

#include "TimePlot.hpp"

auto TimePlotWindowManager::CreateSlot(string name) -> TimePlotSlotProxy
{
    VerifyMainThread();

    auto data = make_shared<TimePlot::SlotData>();

    TimePlotSlotProxy proxy;
    proxy._body = data;

    data->name = std::move(name);
    data->pointsPendingUploaded.reserve(256);

    _slots.insert(std::move(data));
    return proxy;
}

void TimePlotWindowManager::TickWindow()
{
    /*
     * 1. 메인 스레드에서 각 슬롯 프록시가 데이터 업로드 -> SlotData.pointsPendingUpload
     * 2. 각 프레임 윈도우 서술자 업데이트 (가시 영역). 변경 시 dirty 마크
     * 3. 주기적으로 메인 스레드에서 각 슬롯 iterate -> 타겟 윈도우가 dirty하거나, 업로드된 데이터 있으면
     *    async.targets에 해당 슬롯데이터 복사 및 async.allValues에 업로드 된 데이터 복사.
     * as.4. 각 슬롯의 allValues를 윈도우의 프레임 영역에 맞게, 밀도를 고려해 샘플링 후 cacheBuild에 넣는다.
     *       이후 각 슬롯의 axis range 값을 올바르게 설정.
     * as.5. 메인 스레드에 _fnMainThreadSwapBuffer 함수 퍼블리시.
     * 6. 메인 스레드에서 cache swap 수행.
     */
    for (auto& s : _slots) { s->pointsPendingUploaded.clear(); }  // TODO: Replace this with actual logic

    /// Render
}
