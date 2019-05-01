#include "McRave.h"
#include "EventManager.h"

using namespace BWAPI;
using namespace std;

namespace McRave::Units {

    namespace {

        set<shared_ptr<UnitInfo>> enemyUnits;
        set<shared_ptr<UnitInfo>> myUnits;
        set<shared_ptr<UnitInfo>> neutralUnits;
        set<shared_ptr<UnitInfo>> allyUnits;
        map<UnitSizeType, int> allySizes;
        map<UnitSizeType, int> enemySizes;
        map<UnitType, int> enemyTypes;
        map<UnitType, int> myVisibleTypes;
        map<UnitType, int> myCompleteTypes;
        map<Role, int> myRoles;
        set<Unit> splashTargets;
        double immThreat, proxThreat;
        int supply = 0;
        int scoutDeadFrame = 0;

        void resetValues()
        {
            immThreat = 0.0;
            proxThreat = 0.0;
            splashTargets.clear();
            enemyTypes.clear();
            myVisibleTypes.clear();
            myCompleteTypes.clear();
            supply = 0;

            enemyUnits.clear();
            myUnits.clear();
            neutralUnits.clear();
            allyUnits.clear();
        }

        void updateRole(const shared_ptr<UnitInfo>& u)
        {
            auto &unit = *u;

            // Don't assign a role to uncompleted units
            if (!unit.unit()->isCompleted() && !unit.getType().isBuilding() && unit.getType() != UnitTypes::Zerg_Egg) {
                unit.setRole(Role::None);
                return;
            }

            // Store old role to update counters after
            auto oldRole = unit.getRole();

            // Update default role
            if (unit.getRole() == Role::None) {
                if (unit.getType().isWorker())
                    unit.setRole(Role::Worker);
                else if ((unit.getType().isBuilding() && unit.getGroundDamage() == 0.0 && unit.getAirDamage() == 0.0) || unit.getType() == UnitTypes::Zerg_Larva || unit.getType() == UnitTypes::Zerg_Egg)
                    unit.setRole(Role::Production);
                else if (unit.getType().isBuilding() && unit.getGroundDamage() != 0.0 && unit.getAirDamage() != 0.0)
                    unit.setRole(Role::Defender);
                else if (unit.getType().spaceProvided() > 0)
                    unit.setRole(Role::Transport);
                else
                    unit.setRole(Role::Combat);
            }

            // Check if workers should fight or work
            if (unit.getType().isWorker()) {
                if (unit.getRole() == Role::Worker && !unit.unit()->isCarryingMinerals() && !unit.unit()->isCarryingGas() && (Util::reactivePullWorker(unit) || Util::proactivePullWorker(unit) || Util::pullRepairWorker(unit))) {
                    unit.setRole(Role::Combat);
                    Players::addStrength(unit);
                    unit.setBuildingType(UnitTypes::None);
                    unit.setBuildPosition(TilePositions::Invalid);
                }
                else if (unit.getRole() == Role::Combat && !Util::reactivePullWorker(unit) && !Util::proactivePullWorker(unit) && !Util::pullRepairWorker(unit))
                    unit.setRole(Role::Worker);

                if (Util::reactivePullWorker(unit))
                    unit.circleBlack();
                if (Util::proactivePullWorker(unit))
                    unit.circleBlue();
                if (Util::pullRepairWorker(unit))
                    unit.circleGreen();
            }

            // Check if an overlord should scout or support
            if (unit.getType() == UnitTypes::Zerg_Overlord) {
                if (!Terrain::foundEnemy())
                    unit.setRole(Role::Scout);
                else
                    unit.setRole(Role::Support);
                return;
            }

            // Check if this unit should scout
            if (BWEB::Map::getNaturalChoke() && BuildOrder::shouldScout() && Units::getMyRoleCount(Role::Scout) < Scouts::getScoutCount() && Broodwar->getFrameCount() - scoutDeadFrame > 240) {
                auto &scout = Util::getClosestUnitGround(Position(BWEB::Map::getNaturalChoke()->Center()), PlayerState::Self, [&](auto &u) {
                    return u.getRole() == Role::Worker && (!u.hasResource() || !u.getResource().getType().isRefinery()) && u.getBuildingType() == UnitTypes::None && !u.unit()->isCarryingMinerals() && !u.unit()->isCarryingGas();
                });

                if (scout) {
                    scout->setRole(Role::Scout);
                    scout->setBuildingType(UnitTypes::None);
                    scout->setBuildPosition(TilePositions::Invalid);

                    if (scout->hasResource())
                        Workers::removeUnit(*scout);
                }
            }
            else if (Units::getMyRoleCount(Role::Scout) > Scouts::getScoutCount()) {

                // Look at scout targets and find the least useful scout, remove it
                auto &target = Strategy::enemyProxy() || !Terrain::getEnemyStartingPosition().isValid() ? BWEB::Map::getMainPosition() : Terrain::getEnemyStartingPosition();
                auto &scout = Util::getClosestUnitGround(target, PlayerState::Self, [&](auto &u) {
                    return u.getRole() == Role::Scout;
                });

                if (scout) {
                    scout->setRole(Role::Worker);
                }
            }

            // Check if a worker morphed into a building
            if (unit.getRole() == Role::Worker && unit.getType().isBuilding()) {
                if (unit.getType().isBuilding() && unit.getGroundDamage() == 0.0 && unit.getAirDamage() == 0.0)
                    unit.setRole(Role::Production);
                else
                    unit.setRole(Role::Combat);
            }

            // Detectors and Support roles
            if ((unit.getType().isDetector() && !unit.getType().isBuilding()) || unit.getType() == UnitTypes::Protoss_Arbiter)
                unit.setRole(Role::Support);

            // Increment new role counter, decrement old role counter
            auto newRole = unit.getRole();
            if (oldRole != newRole) {
                if (oldRole != Role::None)
                    myRoles[oldRole] --;
                if (newRole != Role::None)
                    myRoles[newRole] ++;
            }
        }

        void updateEnemies()
        {
            // Enemy
            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;
                if (!player.isEnemy())
                    continue;

                for (auto &u : player.getUnits()) {
                    UnitInfo &unit = *u;
                    enemyUnits.insert(u);

                    Broodwar->drawTextMap(unit.getPosition(), "%.3f", unit.getPriority());

                    // If this is a flying building that we haven't recognized as being a flyer, remove overlap tiles
                    auto flyingBuilding = unit.unit()->exists() && !unit.isFlying() && (unit.unit()->getOrder() == Orders::LiftingOff || unit.unit()->getOrder() == Orders::BuildingLiftOff || unit.unit()->isFlying());
                    if (flyingBuilding && unit.getLastTile().isValid())
                        Events::customOnUnitLift(unit);

                    // If unit is visible, update it
                    if (unit.unit()->exists()) {
                        unit.update();

                        // TODO: Move to a UnitInfo flag
                        if (unit.hasTarget() && (unit.getType() == UnitTypes::Terran_Vulture_Spider_Mine || unit.getType() == UnitTypes::Protoss_Scarab))
                            splashTargets.insert(unit.getTarget().unit());

                        if (unit.getType().isBuilding() && !unit.isFlying() && BWEB::Map::isUsed(unit.getTilePosition()) == UnitTypes::None)
                            Events::customOnUnitLand(unit);
                    }

                    // Must see a 3x3 grid of Tiles to set a unit to invalid position
                    if (!unit.unit()->exists() && (!unit.isBurrowed() || Command::overlapsDetection(unit.unit(), unit.getPosition(), PlayerState::Self) || (unit.getWalkPosition().isValid() && Grids::getAGroundCluster(unit.getWalkPosition()) > 0)))
                        Events::customOnUnitDisappear(unit);

                    // If unit has a valid type, update enemy composition tracking
                    if (unit.getType().isValid())
                        enemyTypes[unit.getType()] += 1;

                    // If a unit is threatening our position
                    if (unit.isThreatening() && (unit.getType().groundWeapon().damageAmount() > 0 || unit.getType() == UnitTypes::Terran_Bunker)) {
                        if (unit.getType().isBuilding())
                            immThreat += 1.50;
                        else
                            immThreat += unit.getVisibleGroundStrength();
                    }
                    if (unit.isThreatening())
                        unit.circleRed();
                }
            }
        }

        void updateAllies()
        {

        }

        void updateSelf()
        {
            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;
                if (!player.isSelf())
                    continue;

                for (auto &u : player.getUnits()) {
                    UnitInfo &unit = *u;
                    myUnits.insert(u);

                    unit.update();
                    updateRole(u);

                    auto type = unit.getType() == UnitTypes::Zerg_Egg ? unit.unit()->getBuildType() : unit.getType();
                    if (unit.unit()->isCompleted()) {
                        myCompleteTypes[type] ++;
                        myVisibleTypes[type] ++;
                    }
                    else {
                        myVisibleTypes[type] ++;
                    }

                    supply += type.supplyRequired();
                }
            }
        }

        void updateNeutrals()
        {
            // Neutrals
            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;
                if (!player.isNeutral())
                    continue;

                for (auto &u : player.getUnits()) {
                    UnitInfo &unit = *u;
                    neutralUnits.insert(u);

                    if (!unit.unit() || !unit.unit()->exists())
                        continue;
                }
            }
        }

        void updateUnitSizes()
        {
            allySizes.clear();
            enemySizes.clear();

            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;
                if (player.isSelf()) {
                    for (auto &u : player.getUnits()) {
                        UnitInfo &unit = *u;
                        if (unit.getRole() == Role::Combat)
                            allySizes[unit.getType().size()]++;
                    }
                }
                if (player.isEnemy()) {
                    for (auto &u : player.getUnits()) {
                        UnitInfo &unit = *u;
                        if (!unit.getType().isBuilding() && !unit.getType().isWorker())
                            enemySizes[unit.getType().size()]++;

                    }
                }
            }
        }

        void updateUnits()
        {
            updateEnemies();
            updateAllies();
            updateNeutrals();
            updateSelf();
        }
    }

    void onFrame()
    {
        Visuals::startPerfTest();
        resetValues();
        updateUnitSizes();
        updateUnits();
        Visuals::endPerfTest("Units");
    }

    void storeUnit(Unit unit)
    {
        auto &player = Players::getPlayers()[unit->getPlayer()];
        auto info = UnitInfo();

        for (auto &p : Players::getPlayers()) {
            for (auto &u : p.second.getUnits()) {
                if (u->unit() == unit) {
                    info = *u;
                    return;
                }
            }
        }

        if (unit->getType().isBuilding() && unit->getPlayer() == Broodwar->self()) {
            auto &closestWorker = Util::getClosestUnit(unit->getPosition(), PlayerState::Self, [&](auto &u) {
                return u.getRole() == Role::Worker && u.getBuildPosition() == unit->getTilePosition();
            });
            if (closestWorker) {
                closestWorker->setBuildingType(UnitTypes::None);
                closestWorker->setBuildPosition(TilePositions::Invalid);
            }
        }

        info.setUnit(unit);
        info.update();
        player.getUnits().insert(make_shared<UnitInfo>(info));

        if (unit->getPlayer() == Broodwar->self() && unit->getType() == UnitTypes::Protoss_Pylon)
            Pylons::storePylon(unit);
    }

    void removeUnit(Unit unit)
    {
        BWEB::Map::onUnitDestroy(unit);

        // Find the unit
        for (auto &[_,player] : Players::getPlayers()) {
            for (auto &u : player.getUnits()) {
                if (u->unit() == unit) {

                    // Remove assignments and roles
                    if (u->hasTransport())
                        Transports::removeUnit(*u);
                    if (u->hasResource())
                        Workers::removeUnit(*u);
                    if (u->getRole() != Role::None)
                        myRoles[u->getRole()]--;
                    if (u->getRole() == Role::Scout)
                        scoutDeadFrame = Broodwar->getFrameCount();

                    // Invalidates iterator, must return
                    player.getUnits().erase(u);
                    return;
                }
            }
        }
    }

    void morphUnit(Unit unit)
    {
        // HACK: Changing players is kind of annoying, so we just remove and re-store
        if (unit->getType().isRefinery()) {
            removeUnit(unit);
            storeUnit(unit);
        }

        // Morphing into a Hatchery
        if (unit->getType() == UnitTypes::Zerg_Hatchery)
            Stations::storeStation(unit);

        // Grab the UnitInfo for this unit
        auto &info = Units::getUnitInfo(unit);
        if (info) {
            if (info->hasResource())
                Workers::removeUnit(*info);

            if (info->hasTarget())
                info->setTarget(nullptr);

            info->setBuildingType(UnitTypes::None);
            info->setBuildPosition(TilePositions::Invalid);
        }
    }

    int getEnemyCount(UnitType t)
    {
        // Finds how many of a UnitType the enemy has
        auto itr = enemyTypes.find(t);
        if (itr != enemyTypes.end())
            return itr->second;
        return 0;
    }

    int getNumberMelee()
    {
        auto total = 0;

        // Check for combat workers
        if (Broodwar->getFrameCount() < 10000) {
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;
                if (unit.getRole() == Role::Combat && unit.getType().isWorker())
                    total++;
            }
        }

        total += com(UnitTypes::Protoss_Zealot) + com(UnitTypes::Protoss_Dark_Templar)
            + com(UnitTypes::Terran_Medic) + com(UnitTypes::Terran_Firebat)
            + com(UnitTypes::Zerg_Zergling);

        return total;
    }

    int getNumberRanged()
    {
        return com(UnitTypes::Protoss_Dragoon) + com(UnitTypes::Protoss_Reaver) + com(UnitTypes::Protoss_High_Templar)
            + com(UnitTypes::Terran_Marine) + com(UnitTypes::Terran_Vulture) + com(UnitTypes::Terran_Siege_Tank_Tank_Mode) + com(UnitTypes::Terran_Siege_Tank_Siege_Mode) + com(UnitTypes::Terran_Goliath)
            + com(UnitTypes::Zerg_Hydralisk) + com(UnitTypes::Zerg_Lurker) + com(UnitTypes::Zerg_Defiler);
    }

    const shared_ptr<UnitInfo> getUnitInfo(Unit unit)
    {
        for (auto &[_, player] : Players::getPlayers()) {
            for (auto &u : player.getUnits()) {
                if (u->unit() == unit)
                    return u;
            }
        }
        return nullptr;
    }

    set<shared_ptr<UnitInfo>>& getUnits(PlayerState state)
    {
        switch (state) {

        case PlayerState::Ally:
            return allyUnits;
        case PlayerState::Enemy:
            return enemyUnits;
        case PlayerState::Neutral:
            return neutralUnits;
        case PlayerState::Self:
            return myUnits;
        }
        return set<shared_ptr<UnitInfo>>{};
    }

    set<Unit>& getSplashTargets() { return splashTargets; }
    map<UnitSizeType, int>& getAllySizes() { return allySizes; }
    map<UnitSizeType, int>& getEnemySizes() { return enemySizes; }
    map<UnitType, int>& getEnemyTypes() { return enemyTypes; }
    double getImmThreat() { return immThreat; }
    double getProxThreat() { return proxThreat; }
    int getSupply() { return supply; }
    int getMyRoleCount(Role role) { return myRoles[role]; }
    int getMyVisible(UnitType type) { return myVisibleTypes[type]; }
    int getMyComplete(UnitType type) { return myCompleteTypes[type]; }
}