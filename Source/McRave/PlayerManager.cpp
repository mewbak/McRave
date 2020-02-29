#include "McRave.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace McRave::Players
{
    namespace {
        map <Player, PlayerInfo> thePlayers;
        map <Race, int> raceCount;
        map <PlayerState, map<UnitType, int>> currentTypeCounts;
        map <PlayerState, map<UnitType, int>> totalTypeCounts;

        void update(PlayerInfo& player)
        {
            // Add up the number of each race - HACK: Don't add self for now
            player.update();
            if (!player.isSelf())
                raceCount[player.getCurrentRace()]++;

            auto &typeCounts = currentTypeCounts[player.getPlayerState()];

            for (auto &u : player.getUnits()) {
                auto &unit = *u;

                // If unit has a valid type, update enemy composition tracking
                if (unit.getType().isValid())
                    typeCounts[unit.getType()] += 1;                
            }
        }
    }

    void onStart()
    {
        // Store all players
        for (auto player : Broodwar->getPlayers()) {
            auto race = player->isNeutral() ? Races::None : player->getRace(); // BWAPI returns Zerg for neutral race

            PlayerInfo &p = thePlayers[player];
            p.setPlayer(player);
            p.setStartRace(race);
            p.update();

            if (!p.isSelf())
                raceCount[p.getCurrentRace()]++;
        }
    }

    void onFrame()
    {
        // Clear race count and recount
        currentTypeCounts.clear();
        raceCount.clear();
        for (auto &[_, player] : thePlayers)
            update(player);
    }

    void storeUnit(Unit bwUnit)
    {
        auto p = getPlayerInfo(bwUnit->getPlayer());
        auto unit = UnitInfo(bwUnit);

        if (Units::getUnitInfo(bwUnit))
            return;

        if (bwUnit->getType().isBuilding() && bwUnit->getPlayer() == Broodwar->self()) {
            auto closestWorker = Util::getClosestUnit(bwUnit->getPosition(), PlayerState::Self, [&](auto &u) {
                return u.getRole() == Role::Worker && u.getBuildPosition() == bwUnit->getTilePosition();
            });
            if (closestWorker) {
                closestWorker->setBuildingType(UnitTypes::None);
                closestWorker->setBuildPosition(TilePositions::Invalid);
            }
        }

        if (p) {

            // Setup the UnitInfo and update
            unit.update();
            p->getUnits().insert(make_shared<UnitInfo>(bwUnit));

            if (unit.getPlayer() == Broodwar->self() && unit.getType() == UnitTypes::Protoss_Pylon)
                Pylons::storePylon(bwUnit);

            // Increase total counts
            if (unit.getType() != UnitTypes::Zerg_Zergling && unit.getType() != UnitTypes::Zerg_Scourge)
                totalTypeCounts[p->getPlayerState()][unit.getType()]  += 1;
        }
    }

    void removeUnit(Unit bwUnit)
    {
        BWEB::Map::onUnitDestroy(bwUnit);

        // Find the unit
        for (auto &[_, player] : Players::getPlayers()) {
            for (auto &u : player.getUnits()) {
                if (u->unit() == bwUnit) {

                    // Remove assignments and roles
                    if (u->hasTransport())
                        Transports::removeUnit(*u);
                    if (u->hasResource())
                        Workers::removeUnit(*u);
                    if (u->getRole() == Role::Scout)
                        Scouts::removeUnit(*u);

                    // Invalidates iterator, must return
                    player.getUnits().erase(u);
                    return;
                }
            }
        }
    }

    void morphUnit(Unit bwUnit)
    {
        auto playerInfo = getPlayerInfo(bwUnit->getPlayer());

        // HACK: Changing players is kind of annoying, so we just remove and re-store
        if (bwUnit->getType().isRefinery()) {
            removeUnit(bwUnit);
            storeUnit(bwUnit);
        }

        // Morphing into a Hatchery
        if (bwUnit->getType() == UnitTypes::Zerg_Hatchery)
            Stations::storeStation(bwUnit);

        // Grab the UnitInfo for this unit
        auto &u = Units::getUnitInfo(bwUnit);
        if (u) {
            if (u->hasResource())
                Workers::removeUnit(*u);
            if (u->hasTransport())
                Transports::removeUnit(*u);
            if (u->hasTarget())
                u->setTarget(nullptr);
            if (u->getRole() == Role::Scout)
                Scouts::removeUnit(*u);

            if (u->getType() == UnitTypes::Zerg_Larva) {
                auto type = bwUnit->getBuildType();
                totalTypeCounts[playerInfo->getPlayerState()][u->getType()]--;
                totalTypeCounts[playerInfo->getPlayerState()][type] += 1 + (type == UnitTypes::Zerg_Zergling || type == UnitTypes::Zerg_Scourge);
            }

            u->setBuildingType(UnitTypes::None);
            u->setBuildPosition(TilePositions::Invalid);
        }
    }

    int getCurrentCount(PlayerState state, UnitType type)
    {
        // Finds how many of a UnitType this PlayerState currently has
        auto &list = currentTypeCounts[state];
        auto itr = list.find(type);
        if (itr != list.end())
            return itr->second;
        return 0;
    }

    int getTotalCount(PlayerState state, UnitType type) {

        // Finds how many of a UnitType the PlayerState total has
        auto &list = totalTypeCounts[state];
        auto itr = list.find(type);
        if (itr != list.end())
            return itr->second;
        return 0;
    }

    bool hasDetection(PlayerState state)
    {
        return getCurrentCount(state, Protoss_Observer) > 0
            || getCurrentCount(state, Protoss_Photon_Cannon) > 0
            || getCurrentCount(state, Terran_Science_Vessel) > 0
            || getCurrentCount(state, Terran_Missile_Turret) > 0
            || getCurrentCount(state, Zerg_Overlord) > 0;
    }

    int getSupply(PlayerState state)
    {
        auto combined = 0;
        for (auto &[_, player] : thePlayers) {
            if (player.getPlayerState() == state)
                combined += player.getSupply();
        }
        return combined;
    }

    int getRaceCount(Race race, PlayerState state)
    {
        auto combined = 0;
        for (auto &[_, player] : thePlayers) {
            if (player.getCurrentRace() == race && player.getPlayerState() == state)
                combined += 1;
        }
        return combined;
    }

    Strength getStrength(PlayerState state)
    {
        Strength combined;
        for (auto &[_, player] : thePlayers) {
            if (player.getPlayerState() == state)
                combined += player.getStrength();
        }
        return combined;
    }

    map <Player, PlayerInfo>& getPlayers() { return thePlayers; }

    PlayerInfo * getPlayerInfo(Player player)
    {
        for (auto &[p, info] : thePlayers) {
            if (p == player)
                return &info;
        }
        return nullptr;
    }

    bool vP() { return (thePlayers.size() == 3 && raceCount[Races::Protoss] > 0); }
    bool vT() { return (thePlayers.size() == 3 && raceCount[Races::Terran] > 0); }
    bool vZ() { return (thePlayers.size() == 3 && raceCount[Races::Zerg] > 0); }

    bool PvP() {
        return vP() && Broodwar->self()->getRace() == Races::Protoss;
    }
    bool PvT() {
        return vT() && Broodwar->self()->getRace() == Races::Protoss;
    }
    bool PvZ() {
        return vZ() && Broodwar->self()->getRace() == Races::Protoss;
    }

    bool TvP() {
        return vP() && Broodwar->self()->getRace() == Races::Terran;
    }
    bool TvT() {
        return vT() && Broodwar->self()->getRace() == Races::Terran;
    }
    bool TvZ() {
        return vZ() && Broodwar->self()->getRace() == Races::Terran;
    }

    bool ZvP() {
        return vP() && Broodwar->self()->getRace() == Races::Zerg;
    }
    bool ZvT() {
        return vT() && Broodwar->self()->getRace() == Races::Zerg;
    }
    bool ZvZ() {
        return vZ() && Broodwar->self()->getRace() == Races::Zerg;
    }
}