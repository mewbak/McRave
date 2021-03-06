#pragma once
#include <BWAPI.h>

namespace McRave::Resources
{
    int getMinCount();
    int getGasCount();
    int getIncomeMineral();
    int getIncomeGas();
    bool isMinSaturated();
    bool isGasSaturated();
    std::set <std::shared_ptr<ResourceInfo>>& getMyMinerals();
    std::set <std::shared_ptr<ResourceInfo>>& getMyGas();
    std::set <std::shared_ptr<ResourceInfo>>& getMyBoulders();

    void onFrame();
    void storeResource(BWAPI::Unit);
    void removeResource(BWAPI::Unit);

    std::shared_ptr<ResourceInfo> getResourceInfo(BWAPI::Unit);
}
