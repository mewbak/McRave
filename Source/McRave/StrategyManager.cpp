#include "McRave.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace McRave::Strategy {

    namespace {

        map <UnitType, double> unitScore;

        bool enemyFE = false;
        bool invis = false;
        bool rush = false;
        bool holdChoke = false;

        bool proxy = false;
        bool gasSteal = false;
        bool enemyScout = false;
        bool pressure = false;
        string enemyBuild = "Unknown";
        int poolFrame, lingFrame;
        int enemyGas;
        int enemyFrame;

        int inboundScoutFrame;
        int inboundLingFrame;

        bool goonRange = false;
        bool vultureSpeed = false;

        void updateSituationalBehaviour()
        {
            checkNeedDetection();
            checkEnemyPressure();
            checkEnemyProxy();
            checkEnemyRush();
            checkHoldChoke();
        }

        void checkEnemyRush()
        {
            // Rush builds are immediately aggresive builds
            rush = Units::getSupply() < 80 && (enemyBuild == "TBBS" || enemyBuild == "P2Gate" || enemyBuild == "Z5Pool");
        }

        void checkEnemyPressure()
        {
            // Pressure builds are delayed aggresive builds
            pressure = (enemyBuild == "P4Gate" || enemyBuild == "Z9Pool" || enemyBuild == "TSparks" || enemyBuild == "T3Fact");
        }

        void checkHoldChoke()
        {
            holdChoke = BuildOrder::isFastExpand()
                || Units::getGlobalAllyGroundStrength() > Units::getGlobalEnemyGroundStrength()
                || BuildOrder::isWallNat()
                || BuildOrder::isHideTech()
                || Units::getSupply() > 60
                || Players::vT();
        }

        void checkNeedDetection()
        {
            if (Broodwar->self()->getRace() == Races::Protoss) {
                if (Broodwar->self()->completedUnitCount(Protoss_Observer) > 0)
                    invis = false;
            }
            else if (Broodwar->self()->getRace() == Races::Terran) {
                if (Broodwar->self()->completedUnitCount(Terran_Comsat_Station) > 0)
                    invis = false;
                else if (Units::getEnemyCount(Zerg_Hydralisk) > 0 || Units::getEnemyCount(Zerg_Hydralisk_Den) > 0)
                    invis = true;
            }

            // DTs, Vultures, Lurkers
            invis = (Units::getEnemyCount(Protoss_Dark_Templar) >= 1 || Units::getEnemyCount(Protoss_Citadel_of_Adun) >= 1 || Units::getEnemyCount(Protoss_Templar_Archives) >= 1)
                || (enemyBuild == "P1GateDT")
                || (Units::getEnemyCount(Terran_Ghost) >= 1 || Units::getEnemyCount(Terran_Vulture) >= 4)
                || (Units::getEnemyCount(Zerg_Lurker) >= 1 || (Units::getEnemyCount(Zerg_Lair) >= 1 && Units::getEnemyCount(Zerg_Hydralisk_Den) >= 1 && Units::getEnemyCount(Zerg_Hatchery) <= 0))
                || (enemyBuild == "Z1HatchLurker" || enemyBuild == "Z2HatchLurker" || enemyBuild == "P1GateDT");
        }

        void checkEnemyProxy()
        {
            // Proxy builds are built closer to me than the enemy
            proxy = Units::getSupply() < 80 && (enemyBuild == "PCannonRush" || enemyBuild == "TBunkerRush");
        }

        void updateEnemyBuild()
        {
            if (Players::getPlayers().size() > 1 || (Broodwar->getFrameCount() - enemyFrame > 2000 && enemyFrame != 0 && enemyBuild != "Unknown"))
                return;

            if (enemyFrame == 0 && enemyBuild != "Unknown")
                enemyFrame = Broodwar->getFrameCount();

            for (auto &p : Players::getPlayers()) {
                PlayerInfo &player = p.second;

                if (player.getCurrentRace() == Races::Zerg) {

                    // 5 Hatch build detection
                    if (MyStations().getEnemyStations().size() >= 3 || (Units::getEnemyCount(Zerg_Hatchery) + Units::getEnemyCount(Zerg_Lair) >= 4 && Units::getEnemyCount(Zerg_Drone) >= 14))
                        enemyBuild = "Z5Hatch";

                    // Zergling frame
                    if (lingFrame == 0 && Units::getEnemyCount(Zerg_Zergling) >= 6) {
                        lingFrame = Broodwar->getFrameCount();
                        if (!Terrain().getEnemyStartingPosition().isValid())
                            rush = true;
                    }

                    for (auto &u : Units::getEnemyUnits()) {
                        UnitInfo &unit = u.second;

                        // Monitor gas intake or gas steal
                        if (unit.getType().isRefinery() && unit.unit()->exists()) {
                            if (Terrain().isInAllyTerritory(unit.getTilePosition()))
                                gasSteal = true;
                            else
                                enemyGas = unit.unit()->getInitialResources() - unit.unit()->getResources();
                        }

                        // Zergling build detection and pool timing
                        if (unit.getType() == Zerg_Spawning_Pool) {

                            if (poolFrame == 0 && unit.unit()->exists())
                                poolFrame = Broodwar->getFrameCount() + int(double(unit.getType().buildTime()) * (double(unit.getType().maxHitPoints() - unit.unit()->getHitPoints()) / double(unit.getType().maxHitPoints())));

                            if (poolFrame > 0 && Units::getEnemyCount(Zerg_Spire) == 0 && Units::getEnemyCount(Zerg_Hydralisk_Den) == 0 && Units::getEnemyCount(Zerg_Lair) == 0) {
                                if (enemyGas <= 0 && ((poolFrame < 2500 && poolFrame > 0) || (lingFrame < 3000 && lingFrame > 0)))
                                    enemyBuild = "Z5Pool";
                                else if (Units::getEnemyCount(Zerg_Hatchery) == 1 && enemyGas < 148 && enemyGas >= 50 && Units::getEnemyCount(Zerg_Zergling) >= 8)
                                    enemyBuild = "Z9Pool";
                                else if (Units::getEnemyCount(Zerg_Hatchery) >= 1 && Units::getEnemyCount(Zerg_Drone) <= 11 && Units::getEnemyCount(Zerg_Zergling) >= 8)
                                    enemyBuild = "Z9Pool";
                                else if (Units::getEnemyCount(Zerg_Hatchery) == 3 && enemyGas < 148 && enemyGas >= 100)
                                    enemyBuild = "Z3HatchLing";
                                else
                                    enemyBuild = "Unknown";
                            }
                        }

                        // Hydralisk/Lurker build detection
                        else if (unit.getType() == Zerg_Hydralisk_Den) {
                            if (Units::getEnemyCount(Zerg_Spire) == 0) {
                                if (Units::getEnemyCount(Zerg_Hatchery) == 3)
                                    enemyBuild = "Z3HatchHydra";
                                else if (Units::getEnemyCount(Zerg_Hatchery) == 2)
                                    enemyBuild = "Z2HatchHydra";
                                else if (Units::getEnemyCount(Zerg_Hatchery) == 1)
                                    enemyBuild = "Z1HatchHydra";
                                else if (Units::getEnemyCount(Zerg_Lair) + Units::getEnemyCount(Zerg_Hatchery) == 2)
                                    enemyBuild = "Z2HatchLurker";
                                else if (Units::getEnemyCount(Zerg_Lair) == 1 && Units::getEnemyCount(Zerg_Hatchery) == 0)
                                    enemyBuild = "Z1HatchLurker";
                                else if (Units::getEnemyCount(Zerg_Hatchery) >= 4)
                                    enemyBuild = "Z5Hatch";
                            }
                            else
                                enemyBuild = "Unknown";
                        }

                        // Mutalisk build detection
                        else if (unit.getType() == Zerg_Spire || unit.getType() == Zerg_Lair) {
                            if (Units::getEnemyCount(Zerg_Hydralisk_Den) == 0) {
                                if (Units::getEnemyCount(Zerg_Lair) + Units::getEnemyCount(Zerg_Hatchery) == 3 && Units::getEnemyCount(Zerg_Drone) < 14)
                                    enemyBuild = "Z3HatchMuta";
                                else if (Units::getEnemyCount(Zerg_Lair) + Units::getEnemyCount(Zerg_Hatchery) == 2)
                                    enemyBuild = "Z2HatchMuta";
                            }
                            else if (Units::getEnemyCount(Zerg_Hatchery) >= 4)
                                enemyBuild = "Z5Hatch";
                            else
                                enemyBuild = "Unknown";
                        }
                    }
                }
                if (player.getCurrentRace() == Races::Protoss) {

                    auto noGates = Units::getEnemyCount(Protoss_Gateway) == 0;
                    auto noGas = Units::getEnemyCount(Protoss_Assimilator) == 0;
                    auto noExpand = Units::getEnemyCount(Protoss_Nexus) <= 1;

                    // Detect missing buildings as a potential 2Gate
                    if (Terrain().getEnemyStartingPosition().isValid() && Broodwar->getFrameCount() > 3000 && Broodwar->isExplored((TilePosition)Terrain().getEnemyStartingPosition())) {

                        // Check 2 corners scouted
                        auto topLeft = TilePosition(Util::clipToMap(Terrain().getEnemyStartingPosition() - Position(160, 160)));
                        auto botRight = TilePosition(Util::clipToMap(Terrain().getEnemyStartingPosition() + Position(160, 160) + Position(128, 96)));
                        auto maybeProxy = noGates && noGas && noExpand;

                        Broodwar->drawTextScreen(0, 100, "%d  %d  %d", noGates, noGas, noExpand);

                        if (maybeProxy && ((topLeft.isValid() && Grids::lastVisibleFrame(topLeft) > 0) || (botRight.isValid() && Grids::lastVisibleFrame(botRight) > 0)))
                            enemyBuild = "P2Gate";
                        else if (Units::getEnemyCount(Protoss_Gateway) >= 2 && Units::getEnemyCount(Protoss_Nexus) <= 1 && Units::getEnemyCount(Protoss_Assimilator) <= 0 && Units::getEnemyCount(Protoss_Cybernetics_Core) <= 0 && Units::getEnemyCount(Protoss_Dragoon) <= 0)
                            enemyBuild = "P2Gate";
                        else if (enemyBuild == "P2Gate")
                            enemyBuild = "Unknown";
                    }

                    for (auto &u : Units::getEnemyUnits()) {
                        UnitInfo &unit = u.second;

                        if (Terrain().isInAllyTerritory(unit.getTilePosition()) || (inboundScoutFrame > 0 && inboundScoutFrame - Broodwar->getFrameCount() < 64))
                            enemyScout = true;
                        if (unit.getType().isWorker() && inboundScoutFrame == 0) {
                            auto dist = unit.getPosition().getDistance(BWEB::Map::getMainPosition());
                            inboundScoutFrame = Broodwar->getFrameCount() + int(dist / unit.getType().topSpeed());
                        }

                        // Monitor gas intake or gas steal
                        if (unit.getType().isRefinery() && unit.unit()->exists()) {
                            if (Terrain().isInAllyTerritory(unit.getTilePosition()))
                                gasSteal = true;
                            else
                                enemyGas = unit.unit()->getInitialResources() - unit.unit()->getResources();
                        }

                        // PCannonRush
                        if (unit.getType() == Protoss_Forge) {
                            if (unit.getPosition().getDistance(Terrain().getEnemyStartingPosition()) < 320.0 && Units::getEnemyCount(Protoss_Gateway) == 0)
                                enemyBuild = "PCannonRush";
                            else if (enemyBuild == "PCannonRush")
                                enemyBuild = "Unknown";
                        }

                        // PFFE
                        if (unit.getType() == Protoss_Photon_Cannon && Units::getEnemyCount(Protoss_Robotics_Facility) == 0) {
                            if (unit.getPosition().getDistance((Position)Terrain().getEnemyNatural()) < 320.0)
                                enemyBuild = "PFFE";
                            else if (enemyBuild == "PFFE")
                                enemyBuild = "Unknown";
                        }

                        // P2GateExpand
                        if (unit.getType() == Protoss_Nexus) {
                            if (!Terrain().isStartingBase(unit.getTilePosition()) && Units::getEnemyCount(Protoss_Gateway) >= 2)
                                enemyBuild = "P2GateExpand";
                        }

                        // Proxy Builds
                        if (unit.getType() == Protoss_Gateway || unit.getType() == Protoss_Pylon) {
                            if (Terrain().isInAllyTerritory(unit.getTilePosition()) || unit.getPosition().getDistance(mapBWEM.Center()) < 1280.0 || (BWEB::Map::getNaturalChoke() && unit.getPosition().getDistance((Position)BWEB::Map::getNaturalChoke()->Center()) < 480.0)) {
                                proxy = true;

                                if (Units::getEnemyCount(Protoss_Gateway) >= 2)
                                    enemyBuild = "P2Gate";
                            }
                        }

                        // 1GateCore
                        if (unit.getType() == Protoss_Cybernetics_Core) {
                            if (unit.unit()->isUpgrading())
                                goonRange = true;

                            if (Units::getEnemyCount(Protoss_Robotics_Facility) >= 1 && Units::getEnemyCount(Protoss_Gateway) <= 1)
                                enemyBuild = "P1GateRobo";
                            else if (Units::getEnemyCount(Protoss_Gateway) >= 4)
                                enemyBuild = "P4Gate";
                            else if (Units::getEnemyCount(Protoss_Citadel_of_Adun) >= 1 || Units::getEnemyCount(Protoss_Templar_Archives) >= 1 || (!goonRange && Units::getEnemyCount(Protoss_Dragoon) < 2 && Units::getSupply() > 80))
                                enemyBuild = "P1GateDT";
                        }

                        // Pressure checking
                        if (Broodwar->self()->visibleUnitCount(Protoss_Gateway) >= 3)
                            pressure = true;

                        // Proxy Detection
                        if (unit.getType() == Protoss_Pylon && unit.getPosition().getDistance(Terrain().getPlayerStartingPosition()) < 960.0)
                            proxy = true;

                        // FE Detection
                        if (unit.getType().isResourceDepot() && !Terrain().isStartingBase(unit.getTilePosition()))
                            enemyFE = true;
                    }
                }
                if (player.getCurrentRace() == Races::Terran) {
                    for (auto &u : Units::getEnemyUnits()) {
                        UnitInfo &unit = u.second;

                        // Monitor gas intake or gas steal
                        if (unit.getType().isRefinery() && unit.unit()->exists()) {
                            if (Terrain().isInAllyTerritory(unit.getTilePosition()))
                                gasSteal = true;
                            else
                                enemyGas = unit.unit()->getInitialResources() - unit.unit()->getResources();
                        }

                        // TSiegeExpand
                        if ((unit.getType() == Terran_Siege_Tank_Siege_Mode && Units::getEnemyCount(Terran_Vulture) == 0) || (unit.getType().isResourceDepot() && !Terrain().isStartingBase(unit.getTilePosition()) && Units::getEnemyCount(Terran_Machine_Shop) > 0))
                            enemyBuild = "TSiegeExpand";

                        // Barracks Builds
                        if (unit.getType() == Terran_Barracks) {
                            if (Terrain().isInAllyTerritory(unit.getTilePosition()) || unit.getPosition().getDistance(mapBWEM.Center()) < 1280.0 || (BWEB::Map::getNaturalChoke() && unit.getPosition().getDistance((Position)BWEB::Map::getNaturalChoke()->Center()) < 320))
                                enemyBuild = "TBBS";
                            else
                                enemyBuild = "Unknown";
                        }

                        // Factory Research
                        if (unit.getType() == Terran_Machine_Shop) {
                            if (unit.unit()->exists() && unit.unit()->isUpgrading())
                                vultureSpeed = true;
                        }

                        // FE Detection
                        if (unit.getType().isResourceDepot() && !Terrain().isStartingBase(unit.getTilePosition()))
                            enemyFE = true;
                        if (unit.getType() == Terran_Bunker && unit.getPosition().getDistance(Terrain().getEnemyStartingPosition()) < unit.getPosition().getDistance(Terrain().getPlayerStartingPosition()))
                            enemyFE = true;
                    }

                    // Shallow two
                    if (Broodwar->self()->getRace() == Races::Protoss && Units::getEnemyCount(UnitTypes::Terran_Medic) >= 2)
                        enemyBuild = "ShallowTwo";

                    // Sparks
                    if (Broodwar->self()->getRace() == Races::Zerg && Units::getEnemyCount(Terran_Academy) >= 1 && Units::getEnemyCount(Terran_Engineering_Bay) >= 1)
                        enemyBuild = "TSparks";

                    // Joyo
                    if (Broodwar->getFrameCount() < 9000 && Broodwar->self()->getRace() == Races::Protoss && !enemyFE && (Units::getEnemyCount(UnitTypes::Terran_Machine_Shop) >= 2 || (Units::getEnemyCount(Terran_Marine) >= 4 && Units::getEnemyCount(Terran_Siege_Tank_Tank_Mode) >= 3)))
                        enemyBuild = "TJoyo";

                    if ((Units::getEnemyCount(Terran_Barracks) >= 2 && Units::getEnemyCount(Terran_Refinery) == 0) || (Units::getEnemyCount(Terran_Marine) > 5 && Units::getEnemyCount(Terran_Bunker) <= 0 && Broodwar->getFrameCount() < 6000))
                        enemyBuild = "TBBS";
                    if ((Units::getEnemyCount(Terran_Vulture_Spider_Mine) > 0 && Broodwar->getFrameCount() < 9000) || (Units::getEnemyCount(Terran_Factory) >= 2 && vultureSpeed))
                        enemyBuild = "T3Fact";
                }
            }
        }

        void updateScoring()
        {
            // Reset unit score for toss
            if (Broodwar->self()->getRace() == Races::Protoss) {
                for (auto &unit : unitScore)
                    unit.second = 0;
            }

            // Unit score based off enemy composition	
            for (auto &t : Units::getenemyTypes()) {
                if (t.first.isBuilding())
                    continue;

                // For each type, add a score to production based on the unit count divided by our current unit count
                if (Broodwar->self()->getRace() == Races::Protoss)
                    updateProtossUnitScore(t.first, t.second);
            }

            bool MadMix = Broodwar->self()->getRace() != Races::Protoss;
            if (MadMix)
                updateMadMixScore();

            if (Broodwar->self()->getRace() == Races::Terran)
                unitScore[Terran_Medic] = unitScore[Terran_Marine];

            if (Broodwar->self()->getRace() == Races::Protoss) {

                for (auto &t : unitScore) {
                    t.second = log(t.second);
                }

                unitScore[Protoss_Shuttle] = getUnitScore(Protoss_Reaver);

                if (Broodwar->mapFileName().find("BlueStorm") != string::npos)
                    unitScore[Protoss_Carrier] = unitScore[Protoss_Arbiter];

                if (Players::vP() && Broodwar->getFrameCount() >= 20000 && Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) > 0 && Broodwar->self()->completedUnitCount(Protoss_Templar_Archives) > 0) {
                    unitScore[Protoss_Zealot] = unitScore[Protoss_Dragoon];
                    unitScore[Protoss_Archon] = unitScore[Protoss_Dragoon];
                    unitScore[Protoss_High_Templar] += unitScore[Protoss_Dragoon];
                    unitScore[Protoss_Dragoon] = 0.0;
                }
            }
        }

        void updateProtossUnitScore(UnitType unit, int cnt)
        {
            double size = double(cnt) * double(unit.supplyRequired());

            auto const vis = [&](UnitType t) {
                return max(1.0, (double)Broodwar->self()->visibleUnitCount(t));
            };

            switch (unit)
            {
            case Enum::Terran_Marine:
                unitScore[Protoss_Zealot]				+= (size * 0.35) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.65) / vis(Protoss_Dragoon);
                unitScore[Protoss_High_Templar]			+= (size * 0.90) / vis(Protoss_High_Templar);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Terran_Medic:
                unitScore[Protoss_Zealot]				+= (size * 0.35) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.65) / vis(Protoss_Dragoon);
                unitScore[Protoss_High_Templar]			+= (size * 0.90) / vis(Protoss_High_Templar);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Terran_Firebat:
                unitScore[Protoss_Zealot]				+= (size * 0.35) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.65) / vis(Protoss_Dragoon);
                unitScore[Protoss_High_Templar]			+= (size * 0.90) / vis(Protoss_High_Templar);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Terran_Vulture:
                unitScore[Protoss_Dragoon]				+= (size * 1.00) / vis(Protoss_Dragoon);
                unitScore[Protoss_Observer]				+= (size * 0.70) / vis(Protoss_Observer);
                unitScore[Protoss_Arbiter]				+= (size * 0.15) / vis(Protoss_Arbiter);
                break;
            case Enum::Terran_Goliath:
                unitScore[Protoss_Zealot]				+= (size * 0.25) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.75) / vis(Protoss_Dragoon);
                unitScore[Protoss_Arbiter]				+= (size * 0.70) / vis(Protoss_Arbiter);
                unitScore[Protoss_High_Templar]			+= (size * 0.30) / (Protoss_High_Templar);
                break;
            case Enum::Terran_Siege_Tank_Siege_Mode:
                unitScore[Protoss_Zealot]				+= (size * 0.75) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.25) / vis(Protoss_Dragoon);
                unitScore[Protoss_Arbiter]				+= (size * 0.70) / vis(Protoss_Arbiter);
                unitScore[Protoss_High_Templar]			+= (size * 0.30) / vis(Protoss_High_Templar);
                break;
            case Enum::Terran_Siege_Tank_Tank_Mode:
                unitScore[Protoss_Zealot]				+= (size * 0.75) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.25) / vis(Protoss_Dragoon);
                unitScore[Protoss_Arbiter]				+= (size * 0.70) / vis(Protoss_Arbiter);
                unitScore[Protoss_High_Templar]			+= (size * 0.30) / vis(Protoss_High_Templar);
                break;
            case Enum::Terran_Wraith:
                unitScore[Protoss_Dragoon]				+= (size * 1.00) / vis(Protoss_Dragoon);
                break;
            case Enum::Terran_Science_Vessel:
                unitScore[Protoss_Dragoon]				+= (size * 1.00) / vis(Protoss_Dragoon);
                break;
            case Enum::Terran_Battlecruiser:
                unitScore[Protoss_Dragoon]				+= (size * 1.00) / vis(Protoss_Dragoon);
                break;
            case Enum::Terran_Valkyrie:
                break;

            case Enum::Zerg_Zergling:
                unitScore[Protoss_Zealot]				+= (size * 0.85) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.15) / vis(Protoss_Dragoon);
                unitScore[Protoss_Corsair]				+= (size * 0.60) / vis(Protoss_Corsair);
                unitScore[Protoss_High_Templar]			+= (size * 0.30) / vis(Protoss_High_Templar);
                unitScore[Protoss_Archon]				+= (size * 0.30) / vis(Protoss_Archon);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Zerg_Hydralisk:
                unitScore[Protoss_Zealot]				+= (size * 0.75) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.25) / vis(Protoss_Dragoon);
                unitScore[Protoss_High_Templar]			+= (size * 0.80) / vis(Protoss_High_Templar);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.20) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Zerg_Lurker:
                unitScore[Protoss_Dragoon]				+= (size * 1.00) / vis(Protoss_Dragoon);
                unitScore[Protoss_High_Templar]			+= (size * 1.00) / vis(Protoss_High_Templar);
                unitScore[Protoss_Observer]				+= (size * 1.00) / vis(Protoss_Observer);
                break;
            case Enum::Zerg_Ultralisk:
                unitScore[Protoss_Zealot]				+= (size * 0.25) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.75) / vis(Protoss_Dragoon);
                unitScore[Protoss_Reaver]				+= (size * 0.80) / vis(Protoss_Reaver);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.20) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Zerg_Mutalisk:
                unitScore[Protoss_Corsair]				+= (size * 1.00) / vis(Protoss_Corsair);
                break;
            case Enum::Zerg_Guardian:
                unitScore[Protoss_Dragoon]				+= (size * 1.00) / vis(Protoss_Dragoon);
                unitScore[Protoss_Corsair]				+= (size * 1.00) / vis(Protoss_Corsair);
                break;
            case Enum::Zerg_Devourer:
                break;
            case Enum::Zerg_Defiler:
                unitScore[Protoss_Zealot]				+= (size * 1.00) / vis(Protoss_Zealot);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                unitScore[Protoss_Reaver]				+= (size * 0.90) / vis(Protoss_Reaver);
                break;

            case Enum::Protoss_Zealot:
                unitScore[Protoss_Zealot]				+= (size * 0.05) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.95) / vis(Protoss_Dragoon);
                unitScore[Protoss_Reaver]				+= (size * 0.90) / vis(Protoss_Reaver);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Protoss_Dragoon:
                unitScore[Protoss_Zealot]				+= (size * 0.15) / vis(Protoss_Zealot);
                unitScore[Protoss_Dragoon]				+= (size * 0.85) / vis(Protoss_Dragoon);
                unitScore[Protoss_Reaver]				+= (size * 0.60) / vis(Protoss_Reaver);
                unitScore[Protoss_High_Templar]			+= (size * 0.30) / vis(Protoss_High_Templar);
                unitScore[Protoss_Dark_Templar]			+= (size * 0.10) / vis(Protoss_Dark_Templar);
                break;
            case Enum::Protoss_High_Templar:
                unitScore[Protoss_High_Templar]			+= (size * 1.00) / vis(Protoss_High_Templar);
                break;
            case Enum::Protoss_Dark_Templar:
                unitScore[Protoss_Reaver]				+= (size * 1.00) / vis(Protoss_Reaver);
                unitScore[Protoss_Observer]				+= (size * 1.00) / vis(Protoss_Observer);
                break;
            case Enum::Protoss_Reaver:
                unitScore[Protoss_Reaver]				+= (size * 1.00) / vis(Protoss_Reaver);
                break;
            case Enum::Protoss_Archon:
                unitScore[Protoss_High_Templar]			+= (size * 1.00) / vis(Protoss_High_Templar);
                break;
            case Enum::Protoss_Dark_Archon:
                unitScore[Protoss_High_Templar]			+= (size * 1.00) / vis(Protoss_High_Templar);
                break;
            case Enum::Protoss_Scout:
                if (Terrain().isIslandMap())
                    unitScore[Protoss_Scout]				+= (size * 1.00) / vis(Protoss_Scout);
                break;
            case Enum::Protoss_Carrier:
                if (Terrain().isIslandMap())
                    unitScore[Protoss_Scout]				+= (size * 1.00) / vis(Protoss_Scout);
                break;
            case Enum::Protoss_Arbiter:
                unitScore[Protoss_High_Templar]			+= (size * 1.00) / vis(Protoss_High_Templar);
                break;
            case Enum::Protoss_Corsair:
                unitScore[Protoss_High_Templar]			+= (size * 1.00) / vis(Protoss_High_Templar);
                break;
            }
        }

        void updateTerranUnitScore(UnitType unit, int count)
        {
            //for (auto &t : unitScore)
            //	if (!BuildOrder::isUnitUnlocked(t.first))
            //		t.second = 0.0;


            //for (auto &t : BuildOrder::getUnlockedList()) {
            //	UnitInfo dummy;
            //	dummy.setType(t);
            //	dummy.setPlayer(Broodwar->self());
            //	dummy.setGroundRange(Util::groundRange(dummy));
            //	dummy.setAirRange(Util::airRange(dummy));
            //	dummy.setGroundDamage(Util::groundDamage(dummy));
            //	dummy.setAirDamage(Util::airDamage(dummy));
            //	dummy.setSpeed(Util::speed(dummy));

            //	double dps = unit.isFlyer() ? Util::airDPS(dummy) : Util::groundDPS(dummy);
            //	if (t == Terran_Medic)
            //		dps = 0.775;

            //	if (t == Terran_Dropship)
            //		unitScore[t] = 10.0;

            //	else if (unitScore[t] <= 0.0)
            //		unitScore[t] += (dps*count / max(1.0, (double)Broodwar->self()->visibleUnitCount(t)));
            //	else
            //		unitScore[t] = (unitScore[t] * (9999.0 / 10000.0)) + ((dps * (double)count) / (10000.0 * max(1.0, (double)Broodwar->self()->visibleUnitCount(t))));
            //}
        }

        void updateMadMixScore()
        {

            using namespace UnitTypes;
            vector<UnitType> allUnits;
            if (Broodwar->self()->getRace() == Races::Protoss) {

            }
            else if (Broodwar->self()->getRace() == Races::Terran)
                allUnits.insert(allUnits.end(), { Terran_Marine, Terran_Medic, Terran_Vulture, Terran_Siege_Tank_Tank_Mode, Terran_Goliath, Terran_Wraith, Terran_Dropship, Terran_Science_Vessel, Terran_Valkyrie });
            else if (Broodwar->self()->getRace() == Races::Zerg)
                allUnits.insert(allUnits.end(), { Zerg_Drone, Zerg_Zergling, Zerg_Hydralisk, Zerg_Lurker, Zerg_Mutalisk, Zerg_Scourge });

            // TODO: tier 1,2,3 vectors
            if (Broodwar->getFrameCount() > 20000) {
                if (Broodwar->self()->getRace() == Races::Terran) {
                    allUnits.push_back(Terran_Battlecruiser);
                    allUnits.push_back(Terran_Ghost);
                }
                else if (Broodwar->self()->getRace() == Races::Zerg) {
                    allUnits.push_back(Zerg_Ultralisk);
                    allUnits.push_back(Zerg_Guardian);
                    allUnits.push_back(Zerg_Devourer);
                }
            }

            for (auto &u : Units::getEnemyUnits()) {
                auto &unit = u.second;
                auto type = unit.getType();

                if (Broodwar->self()->getRace() == Races::Zerg && type.isWorker()) {
                    UnitType t = Zerg_Drone;
                    double vis = max(1.0, (double(Broodwar->self()->visibleUnitCount(t))));
                    int s = t.supplyRequired();
                    unitScore[t] = (5.0 / max(1.0, vis));
                }
                else {
                    for (auto &t : allUnits) {

                        UnitInfo dummy;
                        dummy.createDummy(t);

                        if (!unit.getPosition().isValid() || type.isBuilding() || type.isSpell())
                            continue;

                        double myDPS = type.isFlyer() ? Math::airDPS(dummy) : Math::groundDPS(dummy);
                        double enemyDPS = t.isFlyer() ? Math::airDPS(unit) : Math::groundDPS(unit);

                        if (unit.getType() == Terran_Medic)
                            enemyDPS = 0.775;

                        double overallMatchup = enemyDPS > 0.0 ? (myDPS, myDPS / enemyDPS) : myDPS;
                        double distTotal = Terrain().getEnemyStartingPosition().isValid() ? BWEB::Map::getMainPosition().getDistance(Terrain().getEnemyStartingPosition()) : 1.0;
                        double distUnit = Terrain().getEnemyStartingPosition().isValid() ? unit.getPosition().getDistance(BWEB::Map::getMainPosition()) / distTotal : 1.0;

                        if (distUnit == 0.0)
                            distUnit = 0.1;

                        double vis = max(1.0, (double(Broodwar->self()->visibleUnitCount(t))));

                        if (unitScore[t] <= 0.0)
                            unitScore[t] += (overallMatchup / max(1.0, vis * distUnit));
                        else
                            unitScore[t] = (unitScore[t] * (999.0 / 1000.0)) + (overallMatchup / (1000.0 * vis * distUnit));
                    }
                }
            }

            for (auto &u : allUnits)
                unitScore[u] = max(0.1, unitScore[u]);
        }
    }

    void onFrame()
    {
        updateSituationalBehaviour();
        updateEnemyBuild();
        updateScoring();
    }

    double getUnitScore(UnitType unit)
    {
        map<UnitType, double>::iterator itr = unitScore.find(unit);
        if (itr != unitScore.end())
            return itr->second;
        return 0.0;
    }

    UnitType getHighestUnitScore()
    {
        double best = 0.0;
        UnitType bestType = None;
        for (auto &unit : unitScore) {
            if (BuildOrder::isUnitUnlocked(unit.first) && unit.second > best) {
                best = unit.second, bestType = unit.first;
            }
        }

        if (bestType == None && Broodwar->self()->getRace() == Races::Zerg)
            return Zerg_Drone;
        return bestType;
    }

    string getEnemyBuild() { return enemyBuild; }
    bool enemyFastExpand() { return enemyFE; }
    bool enemyRush() { return rush; }
    bool needDetection() { return invis; }
    bool defendChoke() { return holdChoke; }
    bool enemyProxy() { return proxy; }
    bool enemyGasSteal() { return gasSteal; }
    bool enemyScouted() { return enemyScout; }
    bool enemyBust() { return enemyBuild.find("Hydra") != string::npos; }
    bool enemyPressure() { return pressure; }
    int getPoolFrame() { return poolFrame; }
    map <UnitType, double>& getUnitScores() { return unitScore; }
}