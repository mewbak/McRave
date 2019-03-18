#include "McRave.h"

using namespace BWAPI;
using namespace std;

namespace McRave::Resources {

    namespace {

        set<shared_ptr<ResourceInfo>> myMinerals;
        set<shared_ptr<ResourceInfo>> myGas;
        set<shared_ptr<ResourceInfo>> myBoulders;
        bool minSat, gasSat;
        int gasCount;
        int incomeMineral, incomeGas;

        void updateIncome(const shared_ptr<ResourceInfo>& r)
        {
            auto &resource = *r;
            auto cnt = resource.getGathererCount();
            if (resource.getType().isMineralField())
                incomeMineral += cnt == 1 ? 65 : 126;
            else
                incomeGas += resource.getRemainingResources() ? 103 * cnt : 26 * cnt;
        }

        void updateInformation(const shared_ptr<ResourceInfo>& r)
        {
            auto &resource = *r;
            if (resource.unit()->exists())
                resource.updateResource();

            UnitType geyserType = Broodwar->self()->getRace().getRefinery();

            // Update saturation
            if (resource.getType().isMineralField() && minSat && resource.getGathererCount() < 2 && resource.getResourceState() != ResourceState::None)
                minSat = false;
            else if (resource.getType() == geyserType && resource.unit()->isCompleted() && resource.getResourceState() != ResourceState::None && ((BuildOrder::isOpener() && resource.getGathererCount() < min(3, BuildOrder::gasWorkerLimit())) || (!BuildOrder::isOpener() && resource.getGathererCount() < 3)))
                gasSat = false;

            if (!resource.getType().isMineralField() && resource.getResourceState() == ResourceState::Mineable)
                gasCount++;
        }

        void updateResources()
        {
            // Assume saturation, will be changed to false if any resource isn't saturated
            minSat = true, gasSat = true;
            incomeMineral = 0, incomeGas = 0;
            gasCount = 0;

            const auto update = [&](const shared_ptr<ResourceInfo>& r) {
                updateInformation(r);
                updateIncome(r);
            };

            for (auto &r : myBoulders)
                update(r);

            for (auto &r : myMinerals)
                update(r);

            for (auto &r : myGas) {
                auto &resource = *r;
                update(r);

                // If resource is blocked from usage
                if (resource.getTilePosition().isValid()) {
                    for (auto block = mapBWEM.GetTile(resource.getTilePosition()).GetNeutral(); block; block = block->NextStacked()) {
                        if (block && block->Unit() && block->Unit()->exists() && block->Unit()->isInvincible() && !block->IsGeyser())
                            resource.setResourceState(ResourceState::None);
                    }
                }
            }
        }
    }

    void onFrame()
    {
        Visuals::startPerfTest();
        updateResources();
        Visuals::endPerfTest("Resources");
    }

    void storeResource(Unit resource)
    {
        auto info = ResourceInfo();
        auto &list = (resource->getResources() > 0 ? (resource->getType().isMineralField() ? myMinerals : myGas) : myBoulders);

        for (auto &u : list) {
            if (u->unit() == resource)
                return;
        }

        info.setUnit(resource);

        // If we are not on an inital frame, a geyser was just created and we need to see if we own it
        if (Broodwar->getFrameCount() > 0) {
            auto newStation = BWEB::Stations::getClosestStation(resource->getTilePosition());

            if (newStation) {
                for (auto &s : Stations::getMyStations()) {
                    auto &station = *s.second;
                    if (station.getBWEMBase() == newStation->getBWEMBase()) {
                        info.setResourceState(ResourceState::Mineable);
                        break;
                    }
                }
            }
        }
        list.insert(make_shared<ResourceInfo>(info));
    }

    void removeResource(Unit unit)
    {
        auto &resource = getResource(unit);

        if (resource) {
            // Remove assignments
            for (auto &u : resource->targetedByWhat()) {
                if (u)
                    u->setResource(nullptr);
            }

            // Remove dead resources
            if (myMinerals.find(resource) != myMinerals.end())
                myMinerals.erase(resource);
            else if (myBoulders.find(resource) != myBoulders.end())
                myBoulders.erase(resource);
            else if (myGas.find(resource) != myGas.end())
                myGas.erase(resource);
        }
    }

    const shared_ptr<ResourceInfo>& getResource(BWAPI::Unit unit)
    {
        for (auto &m : myMinerals) {
            if (m->unit() == unit)
                return m;
        }
        for (auto &b : myBoulders) {
            if (b->unit() == unit)
                return b;
        }
        for (auto &g : myGas) {
            if (g->unit() == unit)
                return g;
        }
        return nullptr;
    }

    int getGasCount() { return gasCount; }
    int getIncomeMineral() { return incomeMineral; }
    int getIncomeGas() { return incomeGas; }
    bool isMinSaturated() { return minSat; }
    bool isGasSaturated() { return gasSat; }
    set<shared_ptr<ResourceInfo>>& getMyMinerals() { return myMinerals; }
    set<shared_ptr<ResourceInfo>>& getMyGas() { return myGas; }
    set<shared_ptr<ResourceInfo>>& getMyBoulders() { return myBoulders; }
}