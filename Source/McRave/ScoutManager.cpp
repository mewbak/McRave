#include "McRave.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace McRave::Scouts {

    namespace {

        map<Position, ScoutState> scoutTargets;
        map<UnitType, int> currentScoutTypeCounts;
        map<UnitType, int> desiredScoutTypeCounts;
        int scoutDeadFrame = -5000;
        bool fullScout = false;
        UnitType proxyType = None;
        Position proxyPosition = Positions::Invalid;
        vector<Position> edges ={ {-320,-320}, {-480, 0}, {-320, 320}, {0, -480}, {0, 480}, {320, -320}, {480, 0}, {320, 320} };

        void updateScountCount()
        {
            // Set how many of each UnitType are scouts
            desiredScoutTypeCounts.clear();
            currentScoutTypeCounts.clear();
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;
                if (unit.getRole() == Role::Scout)
                    currentScoutTypeCounts[unit.getType()]++;
            }

            // Calculate the number of unexplored bases
            int unexploredBases = 0;
            for (auto &tile : mapBWEM.StartingLocations()) {
                Position center = Position(tile) + Position(64, 48);
                if (!Broodwar->isExplored(TilePosition(center)))
                    unexploredBases++;
            }

            // Protoss
            if (Broodwar->self()->getRace() == Races::Protoss) {
                desiredScoutTypeCounts[Protoss_Probe] = int(BuildOrder::shouldScout() || Strategy::enemyPossibleProxy() || Strategy::enemyProxy());

                // If we are playing PvZ
                if (Players::PvZ()) {

                    // Don't scout vs 4Pool
                    if (Strategy::enemyRush() && Players::getCurrentCount(PlayerState::Enemy, Zerg_Zergling) >= 2 && Players::getSupply(PlayerState::Self) <= 46)
                        desiredScoutTypeCounts[Protoss_Probe] = 0;

                    // Send a 2nd scout after 1st scout
                    else if (!Terrain::getEnemyStartingPosition().isValid() && mapBWEM.StartingLocations().size() == 4 && unexploredBases == 2)
                        desiredScoutTypeCounts[Protoss_Probe] = 2;
                }

                // If we are playing PvP, send a 2nd scout to find any proxies
                if (Players::PvP() && (Strategy::enemyProxy() || Strategy::enemyPossibleProxy()) && com(Protoss_Zealot) < 1)
                    desiredScoutTypeCounts[Protoss_Probe]++;

                // If we are playing PvT, don't scout if we see a pressure build coming
                if (Players::PvT() && Strategy::enemyPressure())
                    desiredScoutTypeCounts[Protoss_Probe] = 0;

                // Check to see if we are contained
                if (BuildOrder::isPlayPassive()) {
                    auto closestEnemy = Util::getClosestUnit(Terrain::getDefendPosition(), PlayerState::Enemy, [&](auto &u) {
                        return !u.getType().isWorker() && u.getGroundDamage() > 0.0;
                    });

                    if (closestEnemy && closestEnemy->getPosition().getDistance(Terrain::getDefendPosition()) < 640.0)
                        desiredScoutTypeCounts[Protoss_Probe] = 0;

                    if (Strategy::enemyPressure() && BuildOrder::isPlayPassive())
                        desiredScoutTypeCounts[Protoss_Probe] = 0;
                }
            }

            // Terran
            if (Broodwar->self()->getRace() == Races::Terran) {
                desiredScoutTypeCounts[Terran_SCV] = (BuildOrder::shouldScout() || Strategy::enemyPossibleProxy() || Strategy::enemyProxy());
            }

            // Zerg
            if (Broodwar->self()->getRace() == Races::Zerg) {
                desiredScoutTypeCounts[Zerg_Drone] = (BuildOrder::shouldScout() || Strategy::enemyPossibleProxy() || Strategy::enemyProxy());
                desiredScoutTypeCounts[Zerg_Overlord] = 0;

                // If we find them, no more Drone scouting
                if ((Players::ZvP() && Util::getTime() > Time(3, 30)) || (Players::ZvT() && Util::getTime() > Time(4, 30)) || Strategy::enemyRush() || Strategy::getEnemyBuild() != "Unknown")
                    desiredScoutTypeCounts[Zerg_Drone] = 0;
                if (Strategy::enemyPressure())
                    desiredScoutTypeCounts[Zerg_Drone] = 0;

                // If enemy can't hit air, send Overlords to scout
                if (Players::getStrength(PlayerState::Enemy).groundToAir <= 0.0
                    && Players::getStrength(PlayerState::Enemy).airToAir <= 0.0
                    && Players::getCurrentCount(PlayerState::Enemy, Protoss_Cybernetics_Core) == 0
                    && Players::getCurrentCount(PlayerState::Enemy, Protoss_Stargate) == 0
                    && Players::getCurrentCount(PlayerState::Enemy, Zerg_Spire) == 0
                    && (unexploredBases <= int(mapBWEM.StartingLocations().size()) - 1 || Terrain::getEnemyStartingPosition().isValid())
                    && (!Players::vT() || !Terrain::getEnemyStartingPosition().isValid()))
                    desiredScoutTypeCounts[Zerg_Overlord] = 2;
            }
        }

        void updateScoutRoles()
        {
            updateScountCount();
            bool sendAnother = (Broodwar->self()->getRace() != Races::Zerg || scoutDeadFrame < 0) && (Broodwar->getFrameCount() - scoutDeadFrame > 240 || (Util::getTime() < Time(4, 0) && Strategy::getEnemyTransition() == "Unknown"));

            const auto assign = [&](UnitType type) {
                shared_ptr<UnitInfo> scout = nullptr;

                // Proxy takes furthest from natural choke
                if (type.isWorker() && BuildOrder::getCurrentOpener() == "Proxy") {
                    scout = Util::getFurthestUnit(Position(BWEB::Map::getNaturalChoke()->Center()), PlayerState::Self, [&](auto &u) {
                        return u.getRole() == Role::Worker && u.getType() == type && (!u.hasResource() || !u.getResource().getType().isRefinery()) && u.getBuildType() == None && !u.unit()->isCarryingMinerals() && !u.unit()->isCarryingGas();
                    });
                }
                else if (type.isFlyer()) {
                    scout = Util::getClosestUnit(Position(BWEB::Map::getNaturalChoke()->Center()), PlayerState::Self, [&](auto &u) {
                        return u.getRole() == Role::Support && u.getType() == type;
                    });
                }
                else if (type.isWorker())
                    scout = Util::getClosestUnitGround(Position(BWEB::Map::getNaturalChoke()->Center()), PlayerState::Self, [&](auto &u) {
                    return u.getRole() == Role::Worker && u.getType() == type && (!u.hasResource() || !u.getResource().getType().isRefinery()) && u.getBuildType() == None && !u.unit()->isCarryingMinerals() && !u.unit()->isCarryingGas();
                });

                if (scout) {
                    scout->setRole(Role::Scout);
                    scout->setBuildingType(None);
                    scout->setBuildPosition(TilePositions::Invalid);

                    if (scout->hasResource())
                        Workers::removeUnit(*scout);
                }
            };

            const auto remove = [&](UnitType type) {
                shared_ptr<UnitInfo> scout = nullptr;
                if (type.isFlyer()) {
                    scout = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Self, [&](auto &u) {
                        return u.getRole() == Role::Scout && u.getType() == type;
                    });
                }
                else {
                    scout = Util::getClosestUnitGround(BWEB::Map::getMainPosition(), PlayerState::Self, [&](auto &u) {
                        return u.getRole() == Role::Scout && u.getType() == type;
                    });
                }

                if (scout)
                    scout->setRole(Role::None);
            };

            for (auto &[type, count] : desiredScoutTypeCounts) {
                if (sendAnother && currentScoutTypeCounts[type] < desiredScoutTypeCounts[type])
                    assign(type);
                else if (currentScoutTypeCounts[type] > desiredScoutTypeCounts[type])
                    remove(type);
            }
        }

        void updateScoutTargets()
        {
            scoutTargets.clear();
            proxyPosition = Positions::Invalid;

            const auto addTarget = [](Position here, ScoutState state) {
                if (here.isValid())
                    scoutTargets.emplace(here, state);
            };

            // If it's a proxy, scout for the proxy building
            if (Strategy::enemyProxy()) {
                auto proxyType = Players::vP() ? Protoss_Pylon : Terran_Barracks;

                if (Strategy::getEnemyBuild() != "CannonRush") {
                    if (Players::getCurrentCount(PlayerState::Enemy, proxyType) == 0) {

                        // Check middle of map
                        addTarget(mapBWEM.Center(), ScoutState::Proxy);
                        proxyPosition = mapBWEM.Center();

                        // Check each closest area around main with unexplored center
                        for (auto &area : mapBWEM.Areas()) {
                            if (!Broodwar->isExplored(TilePosition(area.Top())))
                                addTarget(Position(area.Top()), ScoutState::Proxy);
                        }
                    }
                    else {
                        auto &proxyBuilding = Util::getClosestUnit(mapBWEM.Center(), PlayerState::Enemy, [&](auto &u) {
                            return u.getType() == proxyType;
                        });
                        if (proxyBuilding) {
                            addTarget(proxyBuilding->getPosition(), ScoutState::Proxy);
                            proxyPosition = proxyBuilding->getPosition();
                        }
                    }
                }
                else {

                    auto &proxyBuilding = Util::getClosestUnit(BWEB::Map::getMainPosition(), PlayerState::Enemy, [&](auto &u) {
                        return u.getType() == proxyType && !Terrain::isInEnemyTerritory(u.getTilePosition());
                    });
                    if (proxyBuilding) {
                        addTarget(proxyBuilding->getPosition(), ScoutState::Proxy);
                        proxyPosition = proxyBuilding->getPosition();
                    }
                    else {
                        addTarget(BWEB::Map::getMainPosition() + Position(0, -200), ScoutState::Base);
                        addTarget(BWEB::Map::getMainPosition() + Position(0, 200), ScoutState::Base);
                        addTarget(BWEB::Map::getMainPosition() + Position(-200, 0), ScoutState::Base);
                        addTarget(BWEB::Map::getMainPosition() + Position(200, 0), ScoutState::Base);
                    }
                }
            }

            // If enemy start is valid and explored, add a target to the most recent one to scout
            else if (Terrain::foundEnemy()) {

                // Add each enemy station as a target
                for (auto &[_, station] : Stations::getEnemyStations()) {
                    auto tile = station->getBase()->Center();
                    addTarget(Position(tile), ScoutState::Base);
                }

                // Add extra co-ordinates for better exploring and to determine if we got a full scout off
                int cnt = 0;
                int total = 8;
                for (auto &dir : edges) {
                    auto pos = Terrain::getEnemyStartingPosition() + dir;
                    pos = Util::clipPosition(pos);
                    !pos.isValid() ? total-- : cnt += Grids::lastVisibleFrame(TilePosition(pos)) > 0, addTarget(pos, ScoutState::Base);
                }
                if (cnt >= total - 1)
                    fullScout = true;

                if ((Players::vZ() && Stations::getEnemyStations().size() == 1)
                    || (Players::vP() && Strategy::getEnemyBuild() == "FFE"))
                    addTarget(Position(Terrain::getEnemyExpand()), ScoutState::Expand);
            }

            // If we know where the enemy started
            else if (Terrain::getEnemyStartingTilePosition().isValid())
                addTarget(Terrain::getEnemyStartingPosition(), ScoutState::Base);

            // If we have no idea where the enemy is
            else {
                auto basesExplored = 0;
                multimap<double, Position> startsByDist;

                // Sort unexplored starts by distance
                for (auto &topLeft : mapBWEM.StartingLocations()) {
                    auto center = Position(topLeft) + Position(64, 48);
                    auto dist = BWEB::Map::getGroundDistance(center, BWEB::Map::getMainPosition());

                    auto botRight = topLeft + TilePosition(3, 2);
                    if (!Broodwar->isExplored(topLeft))
                        startsByDist.emplace(dist, Position(topLeft));
                    else if (!Broodwar->isExplored(botRight))
                        startsByDist.emplace(dist, Position(botRight));
                    else
                        basesExplored++;
                }

                // Assign n scout targets where n is scout count
                for (auto &[_, position] : startsByDist)
                    addTarget(position, ScoutState::Base);
                
                // Scout the popular middle proxy location if it's walkable
                if (Broodwar->self()->getRace() == Races::Protoss) {
                    if (basesExplored == 2 && !Players::vZ() && !Terrain::isExplored(mapBWEM.Center()) && BWEB::Map::getGroundDistance(BWEB::Map::getMainPosition(), mapBWEM.Center()) != DBL_MAX)
                        addTarget(mapBWEM.Center(), ScoutState::Proxy);
                }
                else {
                    if (!Players::vZ() && BuildOrder::shouldScout() && !Terrain::isExplored(mapBWEM.Center()) && BWEB::Map::getGroundDistance(BWEB::Map::getMainPosition(), mapBWEM.Center()) != DBL_MAX)
                        addTarget(mapBWEM.Center(), ScoutState::Proxy);
                }
            }

            // Add path between bases in ZvZ or ZvP vs 2Gate
            if (Strategy::getEnemyTransition() != "Unknown" && (Players::ZvZ() || Strategy::getEnemyBuild() == "2Gate") && Terrain::getEnemyStartingPosition().isValid()) {
                for (auto &choke : mapBWEM.GetPath(Terrain::getEnemyStartingPosition(), BWEB::Map::getMainPosition())) {
                    if (Position(choke->Center()).getDistance(Terrain::getEnemyStartingPosition()) < Position(choke->Center()).getDistance(BWEB::Map::getMainPosition()))
                        addTarget(Position(choke->Center()), ScoutState::Rush);
                }
            }
        }

        void updateDestination(UnitInfo& unit)
        {
            auto start = unit.getWalkPosition();
            auto distBest = DBL_MAX;

            const auto isClosestAvailableScout = [&](Position here) {

                shared_ptr<UnitInfo> closest;
                if (unit.getType().isFlyer())
                    closest = Util::getClosestUnit(here, PlayerState::Self, [&](auto &u) {
                    return u.getType().isFlyer() && u.getRole() == Role::Scout;
                });
                else {
                    closest = Util::getClosestUnitGround(here, PlayerState::Self, [&](auto &u) {
                        return !u.getType().isFlyer() && u.getRole() == Role::Scout;
                    });
                }
                return unit.shared_from_this() == closest;
            };

            if (Broodwar->getFrameCount() < 10000) {

                // If it's a center of map proxy
                if ((Strategy::enemyProxy() && proxyPosition.isValid() && isClosestAvailableScout(proxyPosition)) || (!Players::ZvP() && Strategy::enemyPossibleProxy() && unit.getType().isWorker() && isClosestAvailableScout(BWEB::Map::getMainPosition()))) {

                    // Determine what proxy type to expect
                    if (Players::getCurrentCount(PlayerState::Enemy, Terran_Barracks) > 0)
                        proxyType = Terran_Barracks;
                    else if (Players::getCurrentCount(PlayerState::Enemy, Protoss_Pylon) > 0)
                        proxyType = Protoss_Pylon;
                    else if (Players::getCurrentCount(PlayerState::Enemy, Protoss_Gateway) > 0)
                        proxyType = Protoss_Gateway;

                    // Find the closet of the proxy type we expect
                    auto &enemyWorker = Util::getClosestUnit(unit.getPosition(), PlayerState::Enemy, [&](auto u) {
                        return u.getType().isWorker();
                    });
                    auto &enemyStructure = Util::getClosestUnit(unit.getPosition(), PlayerState::Enemy, [&](auto u) {
                        return u.getType() == proxyType;
                    });

                    auto enemyWorkerClose = enemyWorker && enemyWorker->getPosition().getDistance(BWEB::Map::getMainPosition()) < 1280.0;
                    auto enemyWorkerConstructing = enemyWorker && enemyStructure && enemyWorker->getPosition().getDistance(enemyStructure->getPosition()) < 128.0;
                    auto enemyStructureProxy = enemyStructure && !Terrain::isInEnemyTerritory(enemyStructure->getTilePosition());

                    // Attempt to kill the worker if we find it - TODO: Check if the Attack command takes care of this
                    if (Strategy::getEnemyBuild() != "2Gate" && (enemyWorkerClose || enemyWorkerConstructing)) {
                        unit.setDestination(enemyWorker->getPosition());
                        unit.setTarget(&*enemyWorker);
                    }
                    else if (enemyStructureProxy) {
                        unit.setDestination(enemyStructure->getPosition());
                        unit.setTarget(&*enemyStructure);
                    }
                }

                // If we have scout targets, find the closest scout target
                else if (!scoutTargets.empty()) {
                    auto best = 0.0;
                    auto minTimeDiff = 100;
                    for (auto &[target, state] : scoutTargets) {
                        auto dist = target.getDistance(unit.getPosition());
                        auto time = 1 + Grids::lastVisibleFrame((TilePosition)target);
                        auto timeDiff = max(Broodwar->getFrameCount(), 2 * minTimeDiff) - time;
                        auto score = double(time) / dist;

                        if (state == ScoutState::Proxy && unit.getType() == Zerg_Overlord)
                            continue;

                        if (scoutTargets.size() > 1 && (!isClosestAvailableScout(target) || Strategy::enemyProxy()))
                            continue;

                        if (Actions::overlapsActions(unit.unit(), target, None, PlayerState::Self))
                            continue;

                        if (score > best && timeDiff > minTimeDiff) {
                            best = score;
                            unit.setDestination(target);
                        }
                    }

                    // Default to anything close
                    if (!unit.getDestination().isValid()) {
                        for (auto &[target, state] : scoutTargets) {
                            auto dist = target.getDistance(unit.getPosition());
                            auto time = 1 + Grids::lastVisibleFrame((TilePosition)target);
                            auto timeDiff = max(Broodwar->getFrameCount(), 2 * minTimeDiff) - time;
                            auto score = double(time) / dist;

                            if (state == ScoutState::Proxy && unit.getType() == Zerg_Overlord)
                                continue;

                            if (scoutTargets.size() > 1 && (!isClosestAvailableScout(target) || Strategy::enemyProxy()))
                                continue;

                            if (score > best && timeDiff > minTimeDiff) {
                                best = score;
                                unit.setDestination(target);
                            }
                        }
                    }
                }

                if (!unit.getDestination().isValid()) {
                    if (Terrain::getEnemyStartingPosition().isValid())
                        unit.setDestination(Terrain::getEnemyStartingPosition());
                }
            }

            // Make sure we always do something
            else if (!unit.getDestination().isValid())
            {
                int best = INT_MAX;
                for (auto &area : mapBWEM.Areas()) {
                    for (auto &base : area.Bases()) {
                        const auto center = base.Center();

                        if (area.AccessibleNeighbours().size() == 0
                            || Terrain::isInEnemyTerritory(base.Location())
                            || Terrain::isInAllyTerritory(base.Location())
                            || Actions::overlapsActions(unit.unit(), center, None, PlayerState::Self))
                            continue;

                        int time = Grids::lastVisibleFrame(base.Location());
                        if (time < best && isClosestAvailableScout(center)) {
                            best = time;
                            unit.setDestination(center);
                        }
                    }
                }
            }

            // Add Action so other Units dont move to same location
            if (unit.getDestination().isValid())
                Actions::addAction(unit.unit(), unit.getDestination(), unit.getType(), PlayerState::Self);            
        }

        void updatePath(UnitInfo& unit)
        {
            if (!unit.isFlying()) {
                if (unit.getDestination().isValid() && unit.getDestinationPath().getTarget() != TilePosition(unit.getDestination())) {
                    BWEB::Path newPath(unit.getPosition(), unit.getDestination(), unit.getType());
                    newPath.generateJPS([&](const TilePosition &t) { return newPath.terrainWalkable(t); });
                    unit.setDestinationPath(newPath);
                }

                if (unit.getDestinationPath().getTarget() == TilePosition(unit.getDestination())) {
                    auto newDestination = Util::findPointOnPath(unit.getDestinationPath(), [&](Position p) {
                        return p.getDistance(unit.getPosition()) >= 160.0;
                    });

                    if (newDestination.isValid())
                        unit.setDestination(newDestination);
                }
            }
        }

        constexpr tuple commands{ Command::attack, Command::kite, Command::explore };
        void updateDecision(UnitInfo& unit)
        {
            // Convert our commands to strings to display what the unit is doing for debugging
            map<int, string> commandNames{
                make_pair(0, "Attack"),
                make_pair(1, "Kite"),
                make_pair(2, "Explore")
            };

            // Gas steal tester
            if (Broodwar->self()->getName() == "McRaveGasSteal" && Terrain::foundEnemy()) {
                auto gas = Broodwar->getClosestUnit(Terrain::getEnemyStartingPosition(), Filter::GetType == Resource_Vespene_Geyser);
                Broodwar->drawLineMap(gas->getPosition(), unit.getPosition(), Colors::Red);
                if (gas && gas->exists() && gas->getPosition().getDistance(Terrain::getEnemyStartingPosition()) < 320 && unit.getPosition().getDistance(Terrain::getEnemyStartingPosition()) < 160) {
                    if (unit.unit()->getLastCommand().getType() != UnitCommandTypes::Build)
                        unit.unit()->build(Broodwar->self()->getRace().getRefinery(), gas->getTilePosition());
                    return;
                }
                unit.unit()->move(gas->getPosition());
            }

            int width = unit.getType().isBuilding() ? -16 : unit.getType().width() / 2;
            int i = Util::iterateCommands(commands, unit);
            Broodwar->drawTextMap(unit.getPosition() + Position(width, 0), "%c%s", Text::White, commandNames[i].c_str());
        }

        void updateScouts()
        {
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;
                if (unit.getRole() == Role::Scout && unit.isFlying()) {
                    updateDestination(unit);
                    updatePath(unit);
                    updateDecision(unit);
                }
            }

            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;
                if (unit.getRole() == Role::Scout && !unit.isFlying()) {
                    updateDestination(unit);
                    updatePath(unit);
                    updateDecision(unit);
                }
            }
        }
    }

    void onFrame()
    {
        Visuals::startPerfTest();
        updateScoutTargets();
        updateScoutRoles();
        updateScouts();
        Visuals::endPerfTest("Scouts");
    }

    void removeUnit(UnitInfo& unit)
    {
        scoutDeadFrame = Broodwar->getFrameCount();
    }

    bool gotFullScout() { return fullScout; }
}