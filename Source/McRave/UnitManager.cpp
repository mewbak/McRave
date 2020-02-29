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
        map<UnitType, int> myVisibleTypes;
        map<UnitType, int> myCompleteTypes;
        map<UnitSizeType, int> allyGrdSizes;
        map<UnitSizeType, int> enemyGrdSizes;
        map<UnitSizeType, int> allyAirSizes;
        map<UnitSizeType, int> enemyAirSizes;
        map<Role, int> myRoles;
        set<Unit> splashTargets;
        double immThreat;

        void updateRole(UnitInfo& unit)
        {
            // Don't assign a role to uncompleted units
            if (!unit.unit()->isCompleted() && !unit.getType().isBuilding() && unit.getType() != UnitTypes::Zerg_Egg) {
                unit.setRole(Role::None);
                return;
            }

            // Update default role
            if (unit.getRole() == Role::None) {
                if (unit.getType().isWorker())
                    unit.setRole(Role::Worker);
                else if ((unit.getType().isBuilding() && !unit.canAttackGround() && !unit.canAttackAir()) || unit.getType() == UnitTypes::Zerg_Larva || unit.getType() == UnitTypes::Zerg_Egg)
                    unit.setRole(Role::Production);
                else if (unit.getType().isBuilding() && (unit.canAttackGround() || unit.canAttackAir()))
                    unit.setRole(Role::Defender);
                else if ((unit.getType().isDetector() && !unit.getType().isBuilding()) || unit.getType() == UnitTypes::Protoss_Arbiter)
                    unit.setRole(Role::Support);
                else if (unit.getType().spaceProvided() > 0)
                    unit.setRole(Role::Transport);
                else
                    unit.setRole(Role::Combat);
            } 

            // Check if a worker morphed into a building
            if (unit.getRole() == Role::Worker && unit.getType().isBuilding()) {
                if (unit.getType().isBuilding() && !unit.canAttackGround() && !unit.canAttackAir())
                    unit.setRole(Role::Production);
                else
                    unit.setRole(Role::Combat);
            }
        }

        void updateEnemies()
        {
            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;
                if (!player.isEnemy())
                    continue;

                for (auto &u : player.getUnits()) {
                    UnitInfo &unit = *u;
                    enemyUnits.insert(u);

                    // If this is a flying building that we haven't recognized as being a flyer, remove overlap tiles
                    auto flyingBuilding = unit.unit()->exists() && !unit.isFlying() && (unit.unit()->getOrder() == Orders::LiftingOff || unit.unit()->getOrder() == Orders::BuildingLiftOff || unit.unit()->isFlying());
                    if (flyingBuilding && unit.getLastTile().isValid())
                        Events::customOnUnitLift(unit);

                    unit.update();

                    // TODO: Move to a UnitInfo flag
                    if (unit.hasTarget() && (unit.getType() == UnitTypes::Terran_Vulture_Spider_Mine || unit.getType() == UnitTypes::Protoss_Scarab))
                        splashTargets.insert(unit.getTarget().unit());

                    if (unit.getType().isBuilding() && !unit.isFlying() && BWEB::Map::isUsed(unit.getTilePosition()) == UnitTypes::None && Broodwar->isVisible(TilePosition(unit.getPosition())))
                        Events::customOnUnitLand(unit);

                    // Must see a 3x3 grid of Tiles to set a unit to invalid position
                    if (!unit.unit()->exists() && !unit.getType().isBuilding() && (!unit.isBurrowed() || Actions::overlapsDetection(unit.unit(), unit.getPosition(), PlayerState::Self) || (unit.getWalkPosition().isValid() && Grids::getAGroundCluster(unit.getWalkPosition()) > 0)))
                        Events::customOnUnitDisappear(unit);

                    // If a unit is threatening our position
                    if (unit.isThreatening()) {
                        if (unit.getType() == UnitTypes::Protoss_Photon_Cannon)
                            immThreat += 5.00;
                        else if (unit.getType().isBuilding())
                            immThreat += 1.50;
                        else if (!unit.getType().isFlyer())
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
                    updateRole(unit);

                    if (unit.unit()->isSelected())
                        Visuals::displayPath(unit.getPath().getTiles());

                    auto type = unit.getType() == UnitTypes::Zerg_Egg ? unit.unit()->getBuildType() : unit.getType();
                    if (unit.unit()->isCompleted()) {
                        myCompleteTypes[type] += 1;
                        myVisibleTypes[type] += 1;
                    }
                    else {
                        myVisibleTypes[type] += 1 + (type == UnitTypes::Zerg_Zergling || type == UnitTypes::Zerg_Scourge);
                    }
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

                    unit.update();

                    if (!unit.unit() || !unit.unit()->exists())
                        continue;
                }
            }
        }

        void updateCounters()
        {
            immThreat = 0.0;
            splashTargets.clear();
            myVisibleTypes.clear();
            myCompleteTypes.clear();

            enemyUnits.clear();
            myUnits.clear();
            neutralUnits.clear();
            allyUnits.clear();

            allyGrdSizes.clear();
            enemyGrdSizes.clear();

            myRoles.clear();

            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;
                if (player.isSelf()) {
                    for (auto &u : player.getUnits()) {
                        UnitInfo &unit = *u;

                        myRoles[unit.getRole()]++;

                        if (unit.getRole() == Role::Combat)
                            unit.getType().isFlyer() ? allyAirSizes[unit.getType().size()]++ : allyGrdSizes[unit.getType().size()]++;
                    }
                }
                if (player.isEnemy()) {
                    for (auto &u : player.getUnits()) {
                        UnitInfo &unit = *u;
                        if (!unit.getType().isBuilding() && !unit.getType().isWorker())
                            unit.getType().isFlyer() ? enemyAirSizes[unit.getType().size()]++ : enemyGrdSizes[unit.getType().size()]++;

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
        updateCounters();
        updateUnits();
        Visuals::endPerfTest("Units");
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
        return myUnits;
    }

    map<UnitSizeType, int>& getAllyGrdSizes() { return allyGrdSizes; }
    map<UnitSizeType, int>& getEnemyGrdSizes() { return enemyGrdSizes; }
    map<UnitSizeType, int>& getAllyAirSizes() { return allyAirSizes; }
    map<UnitSizeType, int>& getEnemyAirSizes() { return enemyAirSizes; }
    set<Unit>& getSplashTargets() { return splashTargets; }
    double getImmThreat() { return immThreat; }
    int getMyRoleCount(Role role) { return myRoles[role]; }
    int getMyVisible(UnitType type) { return myVisibleTypes[type]; }
    int getMyComplete(UnitType type) { return myCompleteTypes[type]; }
}