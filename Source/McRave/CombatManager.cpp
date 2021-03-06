#include "McRave.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace McRave::Combat {

    namespace {
        int lastRoleChange = 0;
        set<Position> retreatPositions;
        multimap<double, Position> combatClusters;
        map<const BWEM::ChokePoint*, vector<WalkPosition>> concaveCache;

        BWEB::Path airClusterPath;
        pair<double, Position> airCluster;
        pair<UnitCommandType, Position> airCommanderCommand;

        constexpr tuple commands{ Command::misc, Command::special, Command::attack, Command::approach, Command::kite, Command::defend, Command::explore, Command::escort, Command::retreat, Command::move };

        Position findConcavePosition(UnitInfo& unit, BWEM::Area const * area, BWEM::ChokePoint const * choke)
        {
            // Force ranged concaves if enemy has ranged units (defending only)
            const auto enemyRangeExists = Players::getTotalCount(PlayerState::Enemy, UnitTypes::Protoss_Dragoon) > 0
                || Players::getTotalCount(PlayerState::Enemy, UnitTypes::Zerg_Hydralisk) > 0
                || Players::vT();

            // Don't try concaves without chokepoints for now (can use lines in future)
            if (!choke)
                return unit.getPosition();

            auto chokeCount = area->ChokePoints().size();
            auto chokeCenter = Position(choke->Center());
            auto isMelee = unit.getGroundDamage() > 0 && unit.getGroundRange() <= 32.0;
            auto base = area->Bases().empty() ? nullptr : &area->Bases().front();
            auto scoreBest = 0.0;
            auto posBest = unit.getPosition();

            auto useMeleeRadius = isMelee && !enemyRangeExists && Players::getSupply(PlayerState::Self) < 80 && !Players::ZvT();
            auto radius = useMeleeRadius && !Terrain::isDefendNatural() ? 128.0 : (choke->Width() / 2.0);
            auto alreadyValid = false;

            // Choke end nodes and distance to choke center
            auto p1 = Position(choke->Pos(choke->end1));
            auto p2 = Position(choke->Pos(choke->end2));
            auto p1Dist = p1.getDistance(chokeCenter);
            auto p2Dist = p2.getDistance(chokeCenter);

            const auto isValid = [&](WalkPosition w, Position projection) {
                const auto t = TilePosition(w);
                const auto p = Position(w);

                if ((alreadyValid && p.getDistance(unit.getPosition()) > 160.0)
                    || p.getDistance(projection) < radius
                    || p.getDistance(projection) >= 640.0
                    || Buildings::overlapsQueue(unit, p)
                    || !Broodwar->isWalkable(w)
                    || !Util::isTightWalkable(unit, p))
                    return false;
                return true;
            };

            const auto scorePosition = [&](WalkPosition w, Position projection) {
                const auto p = Position(w);

                const auto distProj = exp(p.getDistance(projection));
                const auto distCenter = p.getDistance(chokeCenter);
                const auto distUnit = p.getDistance(unit.getPosition());
                const auto distAreaBase = base ? base->Center().getDistance(p) : 1.0;
                return 1.0 / (distCenter * distAreaBase * distUnit * distProj);
            };

            // Find a position around the center that is suitable        
            auto &tiles = concaveCache[choke];
            for (auto &w : tiles) {
                auto projection = Util::vectorProjection(make_pair(p1, p2), Position(w));

                // Find a vector projection of this point
                auto projDist = projection.getDistance(chokeCenter);

                // Determine if we should lineup at projection or wrap around choke end nodes
                if (chokeCount < 3 && (p1Dist < projDist || p2Dist < projDist))
                    projection = (p1.getDistance(projection) < p2.getDistance(projection)) ? p1 : p2;

                const auto score = scorePosition(w, projection);

                // Swap order after we done debugging
                if (score > scoreBest && isValid(w, projection)) {
                    posBest = Position(w);
                    scoreBest = score;
                }
            }
            return posBest;
        }

        void updateRole(UnitInfo& unit)
        {
            // Can't change role to combat if not a worker or we did one this frame
            if (!unit.getType().isWorker()
                || lastRoleChange == Broodwar->getFrameCount())
                return;

            // Only proactively pull the closest worker to our defend position
            auto closestWorker = Util::getClosestUnit(Terrain::getDefendPosition(), PlayerState::Self, [&](auto &u) {
                return u.getRole() == Role::Worker && (!unit.hasResource() || !unit.getResource().getType().isRefinery()) && !unit.getBuildPosition().isValid();
            });

            const auto combatCount = Units::getMyRoleCount(Role::Combat) - (unit.getRole() == Role::Combat ? 1 : 0);
            const auto combatWorkersCount =  Units::getMyRoleCount(Role::Combat) - com(Protoss_Zealot) - com(Protoss_Dragoon) - com(Zerg_Zergling);
            const auto myGroundStrength = Players::getStrength(PlayerState::Self).groundToGround - (unit.getRole() == Role::Combat ? unit.getVisibleGroundStrength() : 0.0);
            const auto myDefenseStrength = Players::getStrength(PlayerState::Self).groundDefense;
            const auto closestStation = Stations::getClosestStation(PlayerState::Self, unit.getPosition());
            auto arriveAtDefense = unit.timeArrivesWhen();

            const auto healthyWorker = [&] {

                // Don't pull low shield probes
                if (unit.getType() == Protoss_Probe && unit.getShields() < 16)
                    return false;

                // Don't pull low health drones
                if (unit.getType() == Zerg_Drone && unit.getHealth() < 20)
                    return false;
                return true;
            };

            const auto proactivePullWorker = [&]() {

                // If this isn't the closest mineral worker to the defend position, don't pull it
                if (unit.getRole() == Role::Worker && unit.shared_from_this() != closestWorker)
                    return false;

                // Protoss
                if (Broodwar->self()->getRace() == Races::Protoss) {
                    int completedDefenders = com(Protoss_Photon_Cannon) + com(Protoss_Zealot);
                    int visibleDefenders = vis(Protoss_Photon_Cannon) + vis(Protoss_Zealot);

                    // Don't pull workers too early
                    if (arriveAtDefense < Strategy::enemyArrivalTime() - Time(0, 15))
                        return false;

                    // If trying to hide tech, pull 1 probe with a Zealot
                    if (!BuildOrder::isRush() && BuildOrder::isHideTech() && combatCount < 2 && completedDefenders > 0)
                        return true;

                    // If trying to FFE, pull based on Cannon/Zealot numbers, or lack of scouting information
                    if (BuildOrder::getCurrentBuild() == "FFE") {
                        if (Strategy::enemyRush() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 1)
                            return true;
                        if (Strategy::enemyPressure() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 2)
                            return true;
                        if (!Terrain::getEnemyStartingPosition().isValid() && Strategy::getEnemyBuild() == "Unknown" && myGroundStrength < 2.00 && completedDefenders < 1 && visibleDefenders > 0)
                            return true;
                    }

                    // If trying to 2Gate at our natural, pull based on Zealot numbers
                    else if (BuildOrder::getCurrentBuild() == "2Gate" && BuildOrder::getCurrentOpener() == "Natural") {
                        if (Strategy::enemyRush() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 1)
                            return true;
                        if (Strategy::enemyPressure() && combatCount < 8 - (2 * completedDefenders) && visibleDefenders >= 2)
                            return true;
                    }

                    // If trying to 1GateCore and scouted 2Gate late, pull workers to block choke when we are ready
                    else if (BuildOrder::getCurrentBuild() == "1GateCore" && Strategy::getEnemyBuild() == "2Gate" && BuildOrder::getCurrentTransition() != "Defensive" && Strategy::defendChoke()) {
                        if (Util::getTime() < Time(3, 30) && combatWorkersCount < 2)
                            return true;
                    }
                }

                // Terran

                // Zerg
                if (Broodwar->self()->getRace() == Races::Zerg) {

                    // Don't pull low health drones
                    if (unit.getType().isWorker() && unit.getHealth() < 25)
                        return false;
                }
                return false;
            };

            const auto reactivePullWorker = [&]() {

                auto proxyBuildingWorker = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return u.isThreatening() && u.getType().isWorker();
                });
                auto proxyBuilding = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return u.isThreatening() && u.getType().isBuilding();
                });
                auto possibleBuildingWorker = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return proxyBuilding && u.getPosition().getDistance(proxyBuilding->getPosition()) < 160.0;
                });

                // HACK: Don't pull workers reactively versus vultures
                if (Players::getCurrentCount(PlayerState::Enemy, Terran_Vulture) > 0)
                    return false;

                // If this unit has a target that is threatening mining
                if (unit.hasTarget() && unit.getTarget().hasTarget() && unit.getTarget().getTarget().hasResource() && Util::getTime() < Time(10, 0)) {
                    if (unit.getTarget().isThreatening() && unit.getTarget().getTarget().getPosition().getDistance(unit.getTarget().getTarget().getResource().getPosition()) < 32.0)
                        return true;
                }

                if (Units::getImmThreat() > myGroundStrength + myDefenseStrength && Util::getTime() < Time(5, 0))
                    return true;

                // If we have immediate threats
                if (Players::ZvT() && (proxyBuildingWorker || possibleBuildingWorker) && proxyBuilding && com(Zerg_Zergling) <= 0 && combatCount < 8)
                    return true;
                return false;
            };

            // Check if workers should fight or work
            if (unit.getType().isWorker()) {
                if (reactivePullWorker())
                    unit.circleBlue();
                if (proactivePullWorker())
                    unit.circleOrange();
                

                if (unit.getRole() == Role::Worker && !unit.unit()->isCarryingMinerals() && !unit.unit()->isCarryingGas() && healthyWorker() && (reactivePullWorker() || proactivePullWorker())) {
                    unit.setRole(Role::Combat);
                    unit.setBuildingType(None);
                    unit.setBuildPosition(TilePositions::Invalid);
                    lastRoleChange = Broodwar->getFrameCount();
                }
                else if (unit.getRole() == Role::Combat && ((!reactivePullWorker() && !proactivePullWorker()) || !healthyWorker())) {
                    unit.setRole(Role::Worker);
                    lastRoleChange = Broodwar->getFrameCount();
                }
            }
        }

        void updateClusters(UnitInfo& unit)
        {
            // Don't update clusters for fragile combat units
            if (unit.getType() == Protoss_High_Templar
                || unit.getType() == Protoss_Dark_Archon
                || unit.getType() == Protoss_Reaver
                || unit.getType() == Protoss_Interceptor
                || unit.getType() == Zerg_Defiler)
                return;

            // Figure out what type to make the center of our cluster around
            auto clusterAround = UnitTypes::None;
            if (Broodwar->self()->getRace() == Races::Protoss)
                clusterAround = vis(Protoss_Carrier) > 0 ? Protoss_Carrier : Protoss_Corsair;
            else if (Broodwar->self()->getRace() == Races::Zerg)
                clusterAround = vis(Zerg_Guardian) > 0 ? Zerg_Guardian : Zerg_Mutalisk;
            else if (Broodwar->self()->getRace() == Races::Terran)
                clusterAround = vis(Terran_Battlecruiser) > 0 ? Terran_Battlecruiser : Terran_Wraith;

            if (unit.isFlying() && unit.getType() == clusterAround) {
                if (Grids::getAAirCluster(unit.getWalkPosition()) > airCluster.first)
                    airCluster = make_pair(Grids::getAAirCluster(unit.getWalkPosition()), unit.getPosition());
            }
            else if (!unit.isFlying()) {
                const auto strength = Grids::getAGroundCluster(unit.getWalkPosition()) + Grids::getAAirCluster(unit.getWalkPosition());
                combatClusters.emplace(strength, unit.getPosition());
            }
        }

        void updateLocalState(UnitInfo& unit)
        {
            if (!unit.hasTarget()) {
                unit.setLocalState(LocalState::None);
                return;
            }

            const auto simRadius = (unit.getGoal().isValid() && unit.getPosition().getDistance(unit.getGoal()) > unit.getSimRadius()) ? unit.getSimRadius() - 96.0 : unit.getSimRadius();
            const auto closeToSim = unit.getPosition().getDistance(unit.getSimPosition()) < simRadius || Terrain::isInAllyTerritory(unit.getTarget().getTilePosition());
            const auto temporaryRetreat = unit.isLightAir() && unit.hasTarget() && unit.canStartAttack() && !unit.isWithinAngle(unit.getTarget()) && Util::boxDistance(unit.getType(), unit.getPosition(), unit.getTarget().getType(), unit.getTarget().getPosition()) < 48.0;

            // Regardless of any decision, determine if Unit is in danger and needs to retreat
            if (Actions::isInDanger(unit, unit.getPosition())
                || (Actions::isInDanger(unit, unit.getEngagePosition()) && unit.getPosition().getDistance(unit.getEngagePosition()) < simRadius)
                || temporaryRetreat)
                unit.setLocalState(LocalState::Retreat);

            // Regardless of local decision, determine if Unit needs to attack or retreat
            else if (unit.globalEngage())
                unit.setLocalState(LocalState::Attack);            
            else if (unit.globalRetreat())
                unit.setLocalState(LocalState::Retreat);

            // If within local decision range, determine if Unit needs to attack or retreat
            else if (closeToSim) {
                if (unit.localRetreat() || unit.getSimState() == SimState::Loss)
                    unit.setLocalState(LocalState::Retreat);
                else if (unit.localEngage() || unit.getSimState() == SimState::Win)
                    unit.setLocalState(LocalState::Attack);
            }

            // Default state
            else
                unit.setLocalState(LocalState::None);
        }

        void updateGlobalState(UnitInfo& unit)
        {
            bool testingDefense = false;
            if (testingDefense) {
                unit.setGlobalState(GlobalState::Retreat);
                return;
            }

            // Protoss
            if (Broodwar->self()->getRace() == Races::Protoss) {
                if ((!BuildOrder::isFastExpand() && Strategy::enemyFastExpand())
                    || (Strategy::enemyProxy() && !Strategy::enemyRush())
                    || BuildOrder::isRush()
                    || unit.getType() == Protoss_Dark_Templar
                    || (Players::getCurrentCount(PlayerState::Enemy, Protoss_Dark_Templar) > 0 && com(Protoss_Observer) == 0 && Broodwar->getFrameCount() < 15000))
                    unit.setGlobalState(GlobalState::Attack);

                else if (unit.getType().isWorker()
                    || (Broodwar->getFrameCount() < 15000 && BuildOrder::isPlayPassive())
                    || (unit.getType() == Protoss_Corsair && !BuildOrder::firstReady() && Players::getStrength(PlayerState::Enemy).airToAir > 0.0)
                    || (unit.getType() == Protoss_Carrier && com(Protoss_Interceptor) < 16 && !Strategy::enemyPressure()))
                    unit.setGlobalState(GlobalState::Retreat);
                else
                    unit.setGlobalState(GlobalState::Attack);
            }

            // Zerg
            else if (Broodwar->self()->getRace() == Races::Zerg) {
                if (BuildOrder::isRush())
                    unit.setGlobalState(GlobalState::Attack);
                else if ((Broodwar->getFrameCount() < 15000 && BuildOrder::isPlayPassive())
                    || (Players::ZvT() && Broodwar->self()->getUpgradeLevel(UpgradeTypes::Adrenal_Glands) == 0 && unit.getType() == Zerg_Zergling && (Players::getTotalCount(PlayerState::Enemy, Terran_Vulture) > 0 || Strategy::enemyPressure())))
                    unit.setGlobalState(GlobalState::Retreat);
                else
                    unit.setGlobalState(GlobalState::Attack);
            }

            // Terran
            else if (Broodwar->self()->getRace() == Races::Terran) {
                if (BuildOrder::isPlayPassive() || !BuildOrder::firstReady())
                    unit.setGlobalState(GlobalState::Retreat);
                else
                    unit.setGlobalState(GlobalState::Attack);
            }
        }

        void updateDestination(UnitInfo& unit)
        {
            // If attacking and target is close, set as destination
            if (unit.getLocalState() == LocalState::Attack) {
                const auto &intercept = Util::getInterceptPosition(unit);
                if (intercept.getDistance(unit.getTarget().getPosition()) < intercept.getDistance(unit.getPosition()) && (Grids::getMobility(intercept) > 0 || unit.isFlying()))
                    unit.setDestination(intercept);
                else
                    unit.setDestination(unit.getEngagePosition());

                if (unit.getTargetPath().isReachable())
                    unit.setDestinationPath(unit.getTargetPath());
            }

            // If we're globally retreating, set defend position as destination
            else if (unit.getGlobalState() == GlobalState::Retreat && Strategy::defendChoke() /*&& (!unit.hasTarget() || (unit.hasTarget() && (!unit.getTarget().isThreatening() || unit.getGroundRange() > 32.0 || unit.getSpeed() > unit.getTarget().getSpeed())))*/)
                unit.setDestination(findConcavePosition(unit, Terrain::getDefendArea(), Terrain::getDefendChoke()));

            // If retreating, find closest retreat position
            else if (unit.getLocalState() == LocalState::Retreat || unit.getGlobalState() == GlobalState::Retreat) {
                const auto &retreat = getClosestRetreatPosition(unit.getPosition());
                if (retreat.isValid() && (!unit.isLightAir() || Players::getStrength(PlayerState::Enemy).airToAir > 0.0))
                    unit.setDestination(retreat);
                else
                    unit.setDestination(BWEB::Map::getMainPosition());
            }

            // If unit has a goal
            else if (unit.getGoal().isValid())
                unit.setDestination(unit.getGoal());

            // If this is a light air unit, go to the air cluster first if far away
            else if ((unit.isLightAir() || unit.getType() == Zerg_Scourge) && airCluster.second.isValid() && unit.getPosition().getDistance(airCluster.second) > 32.0)
                unit.setDestination(airCluster.second);

            // If this is a light air unit, defend any bases under attack
            else if ((unit.isLightAir() || unit.getType() == Zerg_Scourge) && Units::getImmThreat() > 0.0) {
                auto &attacker = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                    return u.isThreatening();
                });
                if (attacker)
                    unit.setDestination(attacker->getPosition());
            }

            // If this is a light air unit and we can harass
            else if ((unit.isLightAir() || unit.getType() == Zerg_Scourge) && Players::getStrength(PlayerState::Enemy).airToAir * 2 <= Players::getStrength(PlayerState::Self).airToAir) {
                unit.setDestination(Terrain::getHarassPosition());
                unit.setDestinationPath(airClusterPath);
            }

            // If unit has a target and a valid engagement position
            else if (unit.hasTarget() && unit.getEngagePosition().isValid()) {
                unit.setDestination(unit.getEngagePosition());
                unit.setDestinationPath(unit.getTargetPath());
            }

            // If attack position is valid
            else if (Terrain::getAttackPosition().isValid() && unit.canAttackGround())
                unit.setDestination(Terrain::getAttackPosition());

            // If no target and no enemy bases, move to a base location
            else if (!unit.hasTarget() || !unit.getTarget().getPosition().isValid() || unit.unit()->isIdle()) {

                // Finishing enemy off, find remaining bases we haven't scouted
                if (Terrain::getEnemyStartingPosition().isValid()) {
                    auto best = DBL_MAX;

                    for (auto &area : mapBWEM.Areas()) {
                        for (auto &base : area.Bases()) {
                            if (area.AccessibleNeighbours().size() == 0
                                || Terrain::isInAllyTerritory(base.Location()))
                                continue;

                            int time = Grids::lastVisibleFrame(base.Location());
                            if (time < best) {
                                best = time;
                                unit.setDestination(Position(base.Location()));
                            }
                        }
                    }
                }

                // Scouting for enemy base initially
                else {

                    // Sort unexplored starts by distance
                    multimap<double, Position> startsByDist;
                    for (auto &topLeft : mapBWEM.StartingLocations()) {
                        const auto center = Position(topLeft) + Position(64, 48);
                        const auto dist = BWEB::Map::getGroundDistance(center, BWEB::Map::getMainPosition());
                        const auto botRight = topLeft + TilePosition(3, 2);

                        if (!Broodwar->isExplored(botRight) || !Broodwar->isExplored(topLeft))
                            startsByDist.emplace(dist, center);
                    }

                    // Assign closest that isn't assigned
                    int test = INT_MAX;
                    for (auto &[_, position] : startsByDist) {
                        if (!Actions::overlapsActions(unit.unit(), position, Broodwar->self()->getRace().getWorker(), PlayerState::Self) && !Actions::overlapsActions(unit.unit(), position, Zerg_Overlord, PlayerState::Self)) {
                            unit.setDestination(position);
                            break;
                        }
                    }

                    // Assigned furthest
                    if (!unit.getDestination().isValid() && !startsByDist.empty())
                        unit.setDestination(startsByDist.rbegin()->second);

                }
            }

            // Add action so other units dont move to same location
            if (unit.getDestination().isValid())
                Actions::addAction(unit.unit(), unit.getDestination(), None, PlayerState::Self);
        }

        void updatePath(UnitInfo& unit)
        {
            const auto farAway = unit.getPosition().getDistance(airCluster.second) > 128.0;

            const auto flyerAttack = [&](const TilePosition &t) {
                const auto center = Position(t) + Position(16, 16);
                return (farAway || Broodwar->getFrameCount() - Grids::lastVisibleFrame(t) > 250 || unit.getSimState() != SimState::Loss) || center.getDistance(unit.getSimPosition()) > unit.getSimRadius() + 32.0;
            };

            const auto flyerRetreat = [&](const TilePosition &t) {
                const auto center = Position(t) + Position(16, 16);
                return (Grids::getEAirThreat(center) <= 0.0 || center.getDistance(unit.getSimPosition()) > unit.getSimRadius() + 32.0);
            };

            BWEB::Pathfinding::clearCache(flyerAttack); // No caching right now for flying paths
            BWEB::Pathfinding::clearCache(flyerRetreat); // No caching right now for flying paths

            // Generate a new path that obeys collision of terrain and buildings
            if (!unit.isFlying() && !unit.getDestinationPath().getTarget().isValid()) {
                BWEB::Path newPath(unit.getPosition(), unit.getDestination(), unit.getType());
                newPath.generateJPS([&](const TilePosition &t) { return newPath.unitWalkable(t); });
                unit.setDestinationPath(newPath);
            }

            // Generate a flying path for harassing that obeys exploration and staying out of range of threats if possible
            if (unit.isLightAir() && unit.getDestination() == Terrain::getHarassPosition()) {
                BWEB::Path newPath(unit.getPosition(), unit.getDestination(), unit.getType());
                if (unit.getLocalState() != LocalState::Retreat)
                    newPath.generateJPS(flyerAttack);
                unit.setDestinationPath(newPath);
            }

            // If path is reachable, find a point n pixels away to set as new destination
            if (unit.getDestinationPath().isReachable()) {
                auto newDestination = Util::findPointOnPath(unit.getDestinationPath(), [&](Position p) {
                    return p.getDistance(unit.getPosition()) >= 96.0;
                });

                if (newDestination.isValid())
                    unit.setDestination(newDestination);
            }

            // If not reachable, use a point along the BWEM Path
            else if (!unit.isFlying() && !unit.getQuickPath().empty())
                unit.setDestination(Position(unit.getQuickPath().front()->Center()));
        }

        void updateDecision(UnitInfo& unit)
        {
            if (!unit.unit() || !unit.unit()->exists()                                                                                            // Prevent crashes            
                || unit.unit()->isLoaded()
                || unit.unit()->isLockedDown() || unit.unit()->isMaelstrommed() || unit.unit()->isStasised() || !unit.unit()->isCompleted())    // If the unit is locked down, maelstrommed, stassised, or not completed
                return;

            // Convert our commands to strings to display what the unit is doing for debugging
            map<int, string> commandNames{
                make_pair(0, "Misc"),
                make_pair(1, "Special"),
                make_pair(2, "Attack"),
                make_pair(3, "Approach"),
                make_pair(4, "Kite"),
                make_pair(5, "Defend"),
                make_pair(6, "Explore"),
                make_pair(7, "Escort"),
                make_pair(8, "Retreat"),
                make_pair(9, "Move")
            };

            // Iterate commands, if one is executed then don't try to execute other commands
            int width = unit.getType().isBuilding() ? -16 : unit.getType().width() / 2;
            int i = Util::iterateCommands(commands, unit);
            Broodwar->drawTextMap(unit.getPosition() + Position(width, 0), "%c%s", Text::White, commandNames[i].c_str());
        }

        void updateUnits() {
            combatClusters.clear();
            airCluster.first = 0.0;
            airCluster.second = Positions::Invalid;
            multimap<double, UnitInfo&> combatUnitsByDistance;

            // Sort units by distance to destination
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;

                // Don't update if
                if (!unit.unit()->isCompleted()
                    || unit.getType() == Terran_Vulture_Spider_Mine
                    || unit.getType() == Protoss_Scarab
                    || unit.getType() == Protoss_Interceptor
                    || unit.getType().isSpell())
                    continue;

                // Check if we need to pull/push workers to/from combat role
                if (unit.getType().isWorker())
                    updateRole(unit);

                // Update combat role units states and sort by distance to destination
                if (unit.getRole() == Role::Combat) {
                    updateClusters(unit);
                    updateGlobalState(unit);
                    updateLocalState(unit);
                    auto dist = unit.hasTarget() ? 1.0 / unit.getPosition().getDistance(unit.getTarget().getPosition()) : unit.getPosition().getDistance(BWEB::Map::getMainPosition());
                    combatUnitsByDistance.emplace(dist, unit);
                }
            }

            // Get an air commander
            auto airCommander = Util::getClosestUnit(airCluster.second, PlayerState::Self, [&](auto &u) {
                return u.isLightAir() && !u.localRetreat();
            });
            if (airCommander) {

                // Execute the air commanders commands
                Horizon::simulate(*airCommander);
                updateDestination(*airCommander);
                updatePath(*airCommander);
                updateDecision(*airCommander);

                airClusterPath = airCommander->getDestinationPath();

                if (airClusterPath.isReachable())
                    airCommander->circlePurple();

                // Setup air commander commands for other units to follow
                airCommanderCommand = make_pair(airCommander->unit()->getLastCommand().getType(), airCommander->unit()->getLastCommand().getTargetPosition());
            }

            // Execute commands ordered by ascending distance
            for (auto &u : combatUnitsByDistance) {
                auto &unit = u.second;

                // Light air close to the air cluster use the same command of the air commander
                auto chaseDownFastUnit = unit.hasTarget() && unit.getSpeed() < unit.getTarget().getSpeed();
                if (unit.isLightAir() && !unit.isNearSplash() && !chaseDownFastUnit && !unit.localRetreat() && unit.getPosition().getDistance(airCluster.second) < 128.0) {
                    if (unit.hasTarget() && airCommanderCommand.first == UnitCommandTypes::Attack_Unit) {
                        unit.command(UnitCommandTypes::Attack_Unit, unit.getTarget());
                        continue;
                    }
                    else if (airCommanderCommand.first == UnitCommandTypes::Move) {
                        unit.command(UnitCommandTypes::Move, airCommanderCommand.second);
                        continue;
                    }
                }

                // Combat unit decisions
                if (unit.getRole() == Role::Combat) {
                    Horizon::simulate(unit);
                    updateDestination(unit);
                    updatePath(unit);
                    updateDecision(unit);
                }
            }
        }

        void updateRetreatPositions()
        {
            retreatPositions.clear();

            if (Terrain::getDefendChoke() == BWEB::Map::getMainChoke()) {
                retreatPositions.insert(Terrain::getMyMain()->getResourceCentroid());
                return;
            }

            for (auto &[unit, station] : Stations::getMyStations()) {
                auto posBest = Positions::Invalid;
                auto distBest = DBL_MAX;
                auto tile = station->getBase()->Location();

                if (!unit->isCompleted() || unit->isMorphing())
                    continue;

                // Find a TilePosition around it that is suitable to path to
                for (int x = tile.x - 6; x < tile.x + 10; x++) {
                    for (int y = tile.y - 6; y < tile.y + 10; y++) {
                        TilePosition t(x, y);
                        Position center = Position(t) + Position(16, 16);
                        auto dist = center.getDistance(station->getResourceCentroid());
                        if (t.isValid() && dist < distBest && BWEB::Map::isUsed(t) == None) {
                            posBest = center;
                            distBest = dist;
                        }
                    }
                }

                // If valid, add to set of retreat positions
                if (posBest.isValid())
                    retreatPositions.insert(posBest);
            }
        }
    }

    void onStart()
    {
        if (!BWEB::Map::getMainChoke())
            return;

        const auto createCache = [&](const BWEM::ChokePoint * chokePoint, const BWEM::Area * area) {
            auto center = chokePoint->Center();
            for (int x = center.x - 30; x <= center.x + 30; x++) {
                for (int y = center.y - 30; y <= center.y + 30; y++) {
                    WalkPosition w(x, y);
                    const auto p = Position(w) + Position(4, 4);

                    if (!p.isValid()
                        || (area && mapBWEM.GetArea(w) != area)
                        || Grids::getMobility(w) < 6)
                        continue;

                    auto closest = Util::getClosestChokepoint(p);
                    if (closest != chokePoint && p.getDistance(Position(closest->Center())) < 160.0 && (closest == BWEB::Map::getMainChoke() || closest == BWEB::Map::getNaturalChoke()))
                        continue;

                    concaveCache[chokePoint].push_back(w);
                }
            }
        };

        // Main area for defending sometimes is wrong like Andromeda
        const BWEM::Area * defendArea = nullptr;
        auto &[a1, a2] = BWEB::Map::getMainChoke()->GetAreas();
        if (a1 && Terrain::isInAllyTerritory(a1))
            defendArea = a1;
        if (a2 && Terrain::isInAllyTerritory(a2))
            defendArea = a2;

        createCache(BWEB::Map::getMainChoke(), defendArea);
        createCache(BWEB::Map::getMainChoke(), BWEB::Map::getMainArea());

        // Natural area should always be correct
        createCache(BWEB::Map::getNaturalChoke(), BWEB::Map::getNaturalArea());
    }

    void onFrame() {
        Visuals::startPerfTest();
        updateUnits();
        updateRetreatPositions();
        Visuals::endPerfTest("Combat");
    }

    Position getClosestRetreatPosition(Position here)
    {
        auto distBest = DBL_MAX;
        auto posBest = Positions::Invalid;
        for (auto &position : retreatPositions) {
            auto dist = position.getDistance(here);
            if (dist < distBest) {
                posBest = position;
                distBest = dist;
            }
        }
        return posBest;
    }

    multimap<double, Position>& getCombatClusters() { return combatClusters; }

    Position getAirClusterCenter() { return airCluster.second; }
}