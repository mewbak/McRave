#include "McRave.h"
#include "EventManager.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;

namespace McRave::Buildings {
    namespace {

        int queuedMineral, queuedGas;
        int poweredSmall, poweredMedium, poweredLarge;

        map <UnitType, int> morphedThisFrame;
        map <TilePosition, UnitType> buildingsQueued;
        vector<BWEB::Block> unpoweredBlocks;
        map<BWEB::Piece, int> availablePieces;
        map<const BWEM::Area *, int> productionPerArea;
        TilePosition currentExpansion;

        void testResourceDepotSpots(TilePosition& placement, UnitType building)
        {
            vector<BWEB::Station *> stationByScore;
            auto parentStation = BWEB::Stations::getClosestMainStation(BWEB::Map::getMainTile());
            auto allowedFirstMineralBase = (Players::ZvT() || Players::ZvZ() || Players::ZvP()) ? 4 : 3;

            for (int cnt = 0; cnt < int(BWEB::Stations::getStations().size()); cnt++) {
                auto costBest = DBL_MAX;
                BWEB::Station * stationBest = nullptr;

                for (auto &station : BWEB::Stations::getStations()) {
                    auto value = 1.0;
                    auto base = station.getBase();

                    if (find(stationByScore.begin(), stationByScore.end(), &station) != stationByScore.end())
                        continue;

                    if (station.getBase()->Geysers().empty() && int(stationByScore.size()) < allowedFirstMineralBase)
                        continue;

                    auto grdParent = BWEB::Map::getGroundDistance(base->Center(), parentStation->getBase()->Center());
                    auto grdHome = BWEB::Map::getGroundDistance(base->Center(), BWEB::Map::getMainPosition());

                    auto airParent = base->Center().getDistance(parentStation->getBase()->Center());
                    auto airHome = base->Center().getDistance(BWEB::Map::getMainPosition());

                    auto closestEnemyStation = BWEB::Stations::getClosestNaturalStation(Terrain::getEnemyStartingTilePosition());
                    auto airEnemy = closestEnemyStation ? base->Center().getDistance(closestEnemyStation->getBase()->Center()) : base->Center().getDistance(Terrain::getEnemyStartingPosition());
                    auto grdEnemy = closestEnemyStation ? BWEB::Map::getGroundDistance(base->Center(), closestEnemyStation->getBase()->Center()) : BWEB::Map::getGroundDistance(base->Center(), Terrain::getEnemyStartingPosition());

                    auto dist = (grdParent * grdHome * airParent * airHome) / (grdEnemy * airEnemy);
                    if (dist < costBest) {
                        costBest = dist;
                        stationBest = &station;
                    }
                }

                if (stationBest) {
                    stationByScore.push_back(stationBest);
                    parentStation = stationBest;
                }
            }

            int cnt = 0;
            int offset = 0;
            for (auto &station : stationByScore) {
                cnt++;
                offset+=8;

                if (building != None && isBuildable(building, station->getBase()->Location()) && isQueueable(building, station->getBase()->Location())) {
                    placement = station->getBase()->Location();
                    currentExpansion = placement;
                    return;
                }
            }
        }

        bool isDefensiveType(UnitType building)
        {
            return building == Protoss_Photon_Cannon
                || building == Protoss_Shield_Battery
                || building == Zerg_Creep_Colony
                || (Players::ZvZ() && building == Zerg_Spire)
                || building == Terran_Missile_Turret
                || building == Terran_Bunker;
        }

        bool isProductionType(UnitType building)
        {
            return building == Protoss_Gateway
                || building == Protoss_Robotics_Facility
                || building == Protoss_Stargate
                || building == Terran_Barracks
                || building == Terran_Factory
                || building == Terran_Starport
                || building == Zerg_Hatchery;
        }

        bool isTechType(UnitType building)
        {
            return building == Protoss_Forge
                || building == Protoss_Cybernetics_Core
                || building == Protoss_Robotics_Support_Bay
                || building == Protoss_Observatory
                || building == Protoss_Citadel_of_Adun
                || building == Protoss_Templar_Archives
                || building == Protoss_Fleet_Beacon
                || building == Protoss_Arbiter_Tribunal
                || building == Terran_Supply_Depot
                || building == Terran_Engineering_Bay
                || building == Terran_Academy
                || building == Terran_Armory
                || building == Terran_Science_Facility
                || building == Zerg_Spawning_Pool
                || building == Zerg_Evolution_Chamber
                || building == Zerg_Hydralisk_Den
                || building == Zerg_Spire
                || building == Zerg_Queens_Nest
                || building == Zerg_Defiler_Mound
                || building == Zerg_Ultralisk_Cavern;
        }

        bool isWallType(UnitType building)
        {
            return building == Protoss_Forge
                || building == Protoss_Gateway
                || building == Protoss_Pylon
                || building == Terran_Barracks
                || building == Terran_Supply_Depot
                || building == Zerg_Hydralisk_Den
                || building == Zerg_Evolution_Chamber
                || building == Zerg_Hatchery;
        }

        TilePosition returnClosest(UnitType building, set<TilePosition> placements, Position desired, bool purelyClosest = false) {
            auto tileBest = TilePositions::Invalid;
            auto distBest = DBL_MAX;
            for (auto &placement : placements) {
                auto center = Position(placement) + Position(building.tileWidth() * 16, building.tileHeight() * 16);
                auto current = center.getDistance(desired);
                if (current < distBest && isQueueable(building, placement) && (isBuildable(building, placement) || purelyClosest)) {
                    distBest = current;
                    tileBest = placement;
                }
            }
            return tileBest;
        }

        TilePosition returnFurthest(UnitType building, set<TilePosition> placements, Position desired, bool purelyFurthest = false) {
            auto tileBest = TilePositions::Invalid;
            auto distBest = 0.0;
            for (auto &placement : placements) {
                auto center = Position(placement) + Position(building.tileWidth() * 16, building.tileHeight() * 16);
                auto current = center.getDistance(desired);
                if (current > distBest && isQueueable(building, placement) && isBuildable(building, placement)) {
                    distBest = current;
                    tileBest = placement;
                }
            }
            return tileBest;
        }

        TilePosition furthestLocation(UnitType building, Position here)
        {
            auto tileBest = TilePositions::Invalid;
            auto distBest = 0.0;

            // Arrange what set we need to check
            set<TilePosition> placements;
            for (auto &block : BWEB::Blocks::getBlocks()) {
                Position blockCenter = Position(block.getTilePosition()) + Position(block.width() * 16, block.height() * 16);
                if (!Terrain::isInAllyTerritory(block.getTilePosition()))
                    continue;

                if (int(block.getMediumTiles().size()) < 2)
                    continue;

                // Setup placements
                if (building.tileWidth() == 4)
                    placements.insert(block.getLargeTiles().begin(), block.getLargeTiles().end());
                else if (building.tileWidth() == 3)
                    placements.insert(block.getMediumTiles().begin(), block.getMediumTiles().end());
                else
                    placements.insert(block.getSmallTiles().begin(), block.getSmallTiles().end());
            }

            tileBest = returnClosest(building, placements, here);
            return tileBest;
        }

        TilePosition closestLocation(UnitType building, Position here)
        {
            TilePosition tileBest = TilePositions::Invalid;
            double distBest = DBL_MAX;

            // Refineries are only built on my own gas resources
            if (building.isRefinery()) {
                for (auto &g : Resources::getMyGas()) {
                    auto &gas = *g;
                    auto dist = gas.getPosition().getDistance(here);

                    if (Stations::ownedBy(gas.getStation()) != PlayerState::Self
                        || !isQueueable(building, gas.getTilePosition())
                        || !isBuildable(building, gas.getTilePosition())
                        || dist >= distBest)
                        continue;

                    distBest = dist;
                    tileBest = gas.getTilePosition();
                }
                return tileBest;
            }

            // Arrange what set we need to check
            set<TilePosition> placements;
            for (auto &block : BWEB::Blocks::getBlocks()) {
                Position blockCenter = Position(block.getTilePosition()) + Position(block.width() * 16, block.height() * 16);
                if (!Terrain::isInAllyTerritory(block.getTilePosition()) && !BuildOrder::isProxy())
                    continue;

                // --- Move to PylonLocator ---
                if (Broodwar->self()->getRace() == Races::Protoss && building == Protoss_Pylon && vis(Protoss_Pylon) != 1) {
                    bool power = true;
                    bool solo = true;

                    // Check if we are fulfilling power requirements
                    if ((block.getLargeTiles().empty() && poweredLarge == 0) || (block.getMediumTiles().empty() && poweredMedium == 0 && !BuildOrder::isProxy()))
                        power = false;
                    if (poweredLarge > poweredMedium && block.getMediumTiles().empty())
                        power = false;
                    if (poweredMedium > poweredLarge && block.getLargeTiles().empty())
                        power = false;

                    // If we have no powered spots, can't build a solo spot
                    for (auto &small : block.getSmallTiles()) {
                        if (BWEB::Map::isUsed(small) != None
                            || (!Terrain::isInAllyTerritory(small) && !BuildOrder::isProxy() && !Terrain::isIslandMap()))
                            solo = false;
                    }

                    if (!power || !solo)
                        continue;
                }

                // --- Move to TechLocator ---
                // Don't let a Citadel get placed below any larges pieces, risks getting units stuck
                if (building == Protoss_Citadel_of_Adun && !block.getLargeTiles().empty())
                    continue;

                // Likewise, force a Core to be in a start block so we can eventually make a Citadel
                if (building == Protoss_Cybernetics_Core && block.getLargeTiles().empty() && Util::getTime() < Time(5, 0))
                    continue;

                // Setup placements
                if (building.tileWidth() == 4)
                    placements.insert(block.getLargeTiles().begin(), block.getLargeTiles().end());
                else if (building.tileWidth() == 3)
                    placements.insert(block.getMediumTiles().begin(), block.getMediumTiles().end());
                else
                    placements.insert(block.getSmallTiles().begin(), block.getSmallTiles().end());
            }

            tileBest = returnClosest(building, placements, here);
            if (tileBest.isValid())
                return tileBest;

            // --- Move to PylonLocator ---
            // Make sure we always place a pylon if we need large/medium spots or need supply
            if (!tileBest.isValid() && building == Protoss_Pylon) {
                distBest = DBL_MAX;
                for (auto &block : BWEB::Blocks::getBlocks()) {
                    Position blockCenter = Position(block.getTilePosition()) + Position(block.width() * 16, block.height() * 16);

                    if (block.getLargeTiles().size() == 0 && vis(Protoss_Pylon) == 0)
                        continue;
                    if (!Terrain::isInAllyTerritory(block.getTilePosition()))
                        continue;

                    if (!BuildOrder::isOpener() && (!hasPoweredPositions() || vis(Protoss_Pylon) < 20) && (poweredLarge > 0 || poweredMedium > 0)) {
                        if (poweredLarge == 0 && block.getLargeTiles().size() == 0)
                            continue;
                        if (poweredMedium == 0 && block.getMediumTiles().size() == 0)
                            continue;
                    }

                    placements.insert(block.getSmallTiles().begin(), block.getSmallTiles().end());
                }
            }

            tileBest = returnClosest(building, placements, here);

            return tileBest;
        }

        bool findResourceDepotLocation(UnitType building, TilePosition& placement)
        {
            // TODO: Tidy this
            auto hatchCount = vis(Zerg_Hatchery) + vis(Zerg_Lair) + vis(Zerg_Hive);
            auto expand = Broodwar->self()->getRace() != Races::Zerg || (hatchCount > 1 && BuildOrder::shouldExpand()) || (hatchCount <= (1 + BuildOrder::isFastExpand()));
            if (!building.isResourceDepot()
                || !expand)
                return false;

            // If we are expanding, it must be on an expansion area
            const auto baseType = Broodwar->self()->getRace().getResourceDepot();
            auto best = 0.0;

            const auto baseValue = [&](const BWEM::Base& base) {
                auto value = 0.0;
                for (auto &mineral : base.Minerals())
                    value += double(mineral->Amount());
                for (auto &gas : base.Geysers())
                    value += double(gas->Amount());
                if (base.Geysers().size() == 0 && !Terrain::isIslandMap() && int(Stations::getMyStations().size()) < 3)
                    value = value / 1.5;
                return value;
            };

            // Fast expands must be as close to home and have a gas geyser
            if (Stations::getMyStations().size() == 1 && isBuildable(baseType, BWEB::Map::getNaturalTile()) && isQueueable(baseType, BWEB::Map::getNaturalTile())) {
                placement = BWEB::Map::getNaturalTile();
                currentExpansion = placement;
                return true;
            }

            if (Players::ZvP() || Players::ZvT()) {

                auto naturalAt = Players::ZvT() ? 3 : 2;
                auto mainAt = Players::ZvT() ? 2 : 3;

                // Expand to a main or natural if possible
                for (auto &station : BWEB::Stations::getStations()) {

                    if ((station.isMain() && int(Stations::getMyStations().size()) != mainAt) || (station.isNatural() && int(Stations::getMyStations().size()) != naturalAt) || (!station.isNatural() && !station.isMain()))
                        continue;

                    auto &base = *station.getBase();
                    auto value = baseValue(base);

                    // Check if we own the main closest if this is a natural
                    if (station.isNatural() && mainAt < naturalAt) {
                        auto closestMain = BWEB::Stations::getClosestMainStation(base.Location());
                        if (Stations::ownedBy(closestMain) != PlayerState::Self)
                            continue;
                    }

                    // Check if we own the natural closest if this is a main
                    if (station.isNatural() && mainAt < naturalAt) {
                        auto closestNat = BWEB::Stations::getClosestNaturalStation(base.Location());
                        if (Stations::ownedBy(closestNat) != PlayerState::Self)
                            continue;
                    }

                    // Don't try to take an enemy base
                    if (Terrain::getEnemyNatural() == &station)
                        continue;
                    if (Terrain::getEnemyStartingTilePosition() == station.getBase()->Location())
                        continue;

                    auto distance = BWEB::Map::getGroundDistance(BWEB::Map::getMainPosition(), base.Center()) / (BWEB::Map::getGroundDistance(Terrain::getEnemyStartingPosition(), base.Center()));
                    if (isBuildable(baseType, base.Location()) && isQueueable(baseType, base.Location()) && value / distance > best) {
                        best = value / distance;
                        placement = base.Location();
                    }
                }

                if (placement.isValid()) {
                    currentExpansion = placement;
                    return true;
                }
            }

            // Other expansions must be as close to home but as far away from the opponent
            testResourceDepotSpots(placement, building);
            currentExpansion = placement;
            return placement.isValid();
        }

        bool findProductionLocation(UnitType building, TilePosition& placement)
        {
            if (!isProductionType(building)
                || (Broodwar->self()->getRace() == Races::Zerg && !BuildOrder::shouldAddProduction()))
                return false;

            // If against a Zerg rush
            if (Strategy::enemyRush() && (Players::PvZ() || Players::TvZ()))
                placement = furthestLocation(building, (Position)BWEB::Map::getMainChoke()->Center());
            else
                placement = closestLocation(building, Position(BWEB::Map::getMainChoke()->Center()));
            return placement.isValid();
        }

        bool findTechLocation(UnitType building, TilePosition& placement)
        {
            // Don't place if not tech building
            if (!isTechType(building))
                return false;

            // Hide tech if needed or against a Zerg rush
            if ((BuildOrder::isHideTech() && (building == Protoss_Citadel_of_Adun || building == Protoss_Templar_Archives))
                || (Strategy::enemyRush() && (Players::PvZ() || Players::TvZ())))
                placement = furthestLocation(building, (Position)BWEB::Map::getMainChoke()->Center());
            else
                placement = closestLocation(building, BWEB::Map::getMainPosition());
            return placement.isValid();
        }

        bool findDefenseLocation(UnitType building, TilePosition& placement)
        {
            // Don't place if not defensive building
            if (!isDefensiveType(building))
                return false;

            // Defense placements near stations
            for (auto &[_, station] : Stations::getMyStations()) {

                int colonies = 0;
                for (auto& tile : station->getDefenseLocations()) {
                    if (BWEB::Map::isUsed(tile) == Zerg_Creep_Colony || buildingsQueued.find(tile) != buildingsQueued.end())
                        colonies++;
                }

                if (Stations::needGroundDefenses(*station) > colonies) {
                    if (station->isMain()) {
                        placement = returnClosest(building, station->getDefenseLocations(), Position(station->getResourceCentroid()));
                        if (placement.isValid())
                            return true;
                    }
                    else {
                        for (auto &choke : station->getBase()->GetArea()->ChokePoints()) {
                            placement = returnClosest(building, station->getDefenseLocations(), Position(choke->Center()), true);
                            if (placement.isValid() && isBuildable(building, placement))
                                return true;
                            placement = TilePositions::Invalid;
                        }
                    }
                }
                if (Stations::needAirDefenses(*station) > colonies) {
                    placement = returnClosest(building, station->getDefenseLocations(), Position(station->getResourceCentroid()));
                    if (placement.isValid())
                        return true;
                }
            }

            // Defense placements near walls
            for (auto &[_, wall] : BWEB::Walls::getWalls()) {

                auto closestStation = Stations::getClosestStation(PlayerState::Self, wall.getCentroid());
                int colonies = 0;
                for (auto& tile : wall.getDefenses()) {
                    if (BWEB::Map::isUsed(tile) == Zerg_Creep_Colony)
                        colonies++;
                }

                if (Walls::needAirDefenses(wall) > colonies || Walls::needGroundDefenses(wall) > colonies || Broodwar->self()->getRace() == Races::Terran) {
                    placement = returnClosest(building, wall.getDefenses(), closestStation ? (closestStation->getBase()->Center() + wall.getCentroid()) / 2 : wall.getCentroid());
                    if (placement.isValid())
                        return true;
                }
            }
            return false;
        }

        bool findWallLocation(UnitType building, TilePosition& placement)
        {
            // Don't place if not wall building
            if (!isWallType(building))
                return false;

            // Don't wall if not needed
            if ((!BuildOrder::isWallNat() && !BuildOrder::isWallMain()) || (Strategy::enemyBust() && BuildOrder::isOpener()))
                return false;

            // As Zerg, we have to place natural hatch before wall
            if (building == Zerg_Hatchery && isQueueable(building, BWEB::Map::getNaturalTile()) && BWEB::Map::isUsed(BWEB::Map::getNaturalTile()) == UnitTypes::None)
                return false;

            // Find a wall location
            set<TilePosition> placements;
            for (auto &[_, wall] : BWEB::Walls::getWalls()) {

                if (!Terrain::isInAllyTerritory(wall.getArea()))
                    continue;

                // Setup placements
                if (building.tileWidth() == 4)
                    placements.insert(wall.getLargeTiles().begin(), wall.getLargeTiles().end());
                else if (building.tileWidth() == 3)
                    placements.insert(wall.getMediumTiles().begin(), wall.getMediumTiles().end());
                else
                    placements.insert(wall.getSmallTiles().begin(), wall.getSmallTiles().end());
            }

            // Get closest placement
            placement = returnClosest(building, placements, BWEB::Map::getMainPosition());
            return placement.isValid();
        }

        bool findProxyLocation(UnitType building, TilePosition& placement)
        {
            if (!BuildOrder::isOpener() || !BuildOrder::isProxy())
                return false;

            // Find our proxy block
            for (auto &block : BWEB::Blocks::getBlocks()) {
                auto blockCenter = Position(block.getTilePosition() + TilePosition(block.width(), block.height()));
                if (block.isProxy()) {
                    placement = closestLocation(building, blockCenter);
                    return placement.isValid();
                }
            }

            // Otherwise try to find something close to the center and hopefully don't mess up - TODO: Don't mess up better
            placement = closestLocation(building, mapBWEM.Center());
            return placement.isValid();
        }

        bool findBatteryLocation(TilePosition& placement)
        {
            return false;
        }

        bool findPylonLocation(UnitType building, TilePosition& placement)
        {
            if (building != Protoss_Pylon)
                return false;

            // Pylon is separated because we want one unique best buildable position to check, rather than next best buildable position
            const auto stationNeedsPylon = [&](BWEB::Station * station) {
                auto distBest = DBL_MAX;
                auto tileBest = TilePositions::Invalid;
                auto natOrMain = station->isMain() || station->isNatural();

                // Check all defense locations for this station
                for (auto &defense : station->getDefenseLocations()) {
                    double dist = Position(defense).getDistance(Position(station->getResourceCentroid()));
                    if (dist < distBest && (natOrMain || (defense.y <= station->getBase()->Location().y + 1 && defense.y >= station->getBase()->Location().y))) {
                        distBest = dist;
                        tileBest = defense;
                    }
                }
                if (isBuildable(building, tileBest) && isQueueable(building, tileBest))
                    return tileBest;
                return TilePositions::Invalid;
            };

            // Check if this our second Pylon and we're hiding tech
            if (building == Protoss_Pylon && vis(Protoss_Pylon) == 2) {
                placement = furthestLocation(building, (Position)BWEB::Map::getMainChoke()->Center());
                if (placement.isValid())
                    return true;
            }

            // Check if this is our first Pylon versus Protoss
            if (vis(Protoss_Pylon) == 0 && Players::vP() && !BuildOrder::isWallNat() && !BuildOrder::isWallMain()) {
                placement = closestLocation(Protoss_Pylon, (Position)BWEB::Map::getMainChoke()->Center());
                if (placement.isValid())
                    return true;
            }

            // Check if any buildings lost power
            if (!unpoweredBlocks.empty()) {
                for (auto &block : unpoweredBlocks)
                    placement = returnClosest(Protoss_Pylon, block.getSmallTiles(), Position(block.getTilePosition()));

                if (placement.isValid())
                    return true;
            }

            // Check if our main choke should get a Pylon for a Shield Battery
            if (vis(Protoss_Pylon) == 1 && !BuildOrder::isFastExpand() && (!Strategy::enemyRush() || !Players::vZ())) {
                placement = closestLocation(Protoss_Pylon, (Position)BWEB::Map::getMainChoke()->Center());
                if (placement.isValid())
                    return true;
            }

            // Check if we are being busted, add an extra pylon to the defenses
            if (Strategy::enemyBust() && Walls::getNaturalWall() && BuildOrder::isWallNat()) {
                int cnt = 0;
                TilePosition sum(0, 0);
                TilePosition center(0, 0);
                for (auto &defense : Walls::getNaturalWall()->getDefenses()) {
                    if (!Pylons::hasPower(defense, Protoss_Photon_Cannon)) {
                        sum += defense;
                        cnt++;
                    }
                }

                if (cnt != 0) {
                    center = sum / cnt;
                    Position c(center);
                    double distBest = DBL_MAX;

                    // Find unique closest tile to center of defenses
                    for (auto &tile : Walls::getNaturalWall()->getDefenses()) {
                        Position defenseCenter = Position(tile) + Position(32, 32);
                        double dist = defenseCenter.getDistance(c);
                        if (dist < distBest && isQueueable(Protoss_Pylon, tile) && isBuildable(Protoss_Pylon, tile)) {
                            distBest = dist;
                            placement = tile;
                        }
                    }
                }

                if (placement.isValid())
                    return true;
            }

            // Check if any Nexus needs a Pylon for defense placement due to muta rush
            if ((Strategy::getEnemyTransition() == "2HatchMuta" || Strategy::getEnemyTransition() == "3HatchMuta")) {
                for (auto &s : Stations::getMyStations()) {
                    auto &station = *s.second;
                    placement = stationNeedsPylon(&station);
                    if (placement.isValid())
                        return true;
                }
            }

            // Check if we need powered spaces
            if (poweredLarge == 0 || poweredMedium == 0) {
                placement = closestLocation(Protoss_Pylon, BWEB::Map::getMainPosition());
                if (placement.isValid())
                    return true;
            }

            // Check if any Nexus needs a Pylon for defense placement
            if (com(Protoss_Pylon) >= (Players::vT() ? 5 : 3)) {
                for (auto &s : Stations::getMyStations()) {
                    auto &station = *s.second;
                    placement = stationNeedsPylon(&station);
                    if (placement.isValid())
                        return true;
                }
            }

            // Check if we can block an enemy expansion
            if (Util::getTime() > Time(8, 0) && int(Stations::getMyStations().size()) >= 2) {
                placement = Terrain::getEnemyExpand();
                if (placement.isValid() && isBuildable(Protoss_Pylon, placement) && isQueueable(Protoss_Pylon, placement))
                    return true;
            }

            // Resort to finding a production location
            placement = closestLocation(Protoss_Pylon, BWEB::Map::getMainPosition());
            return placement.isValid();
        }

        bool findRefineryLocation(UnitType building, TilePosition& placement)
        {
            if (!building.isRefinery())
                return false;

            placement = closestLocation(building, BWEB::Map::getMainPosition());
            return placement.isValid();
        }

        TilePosition getBuildLocation(UnitType building)
        {
            auto placement = TilePositions::Invalid;

            // Wall
            if (findWallLocation(building, placement))
                return placement;

            // Defensive
            if (findDefenseLocation(building, placement))
                return placement;

            // Proxy
            if (findProxyLocation(building, placement))
                return placement;

            // Resource depot
            if (findResourceDepotLocation(building, placement))
                return placement;

            // Pylon
            if (findPylonLocation(building, placement))
                return placement;

            // Production
            if (findProductionLocation(building, placement))
                return placement;

            // Tech
            if (findTechLocation(building, placement))
                return placement;

            // Refinery
            if (findRefineryLocation(building, placement))
                return placement;

            // HACK: Try to get a placement if we are being horror gated
            if (Strategy::enemyProxy() && Util::getTime() < Time(5, 0) && !isDefensiveType(building) && !building.isResourceDepot())
                placement = Broodwar->getBuildLocation(building, BWEB::Map::getMainTile(), 16);

            return placement;
        }

        void updateCommands(UnitInfo& building)
        {
            const auto willDieToAttacks = [&]() {
                auto possibleDamage = 0;
                for (auto &attacker : building.getTargetedBy()) {
                    if (attacker.lock() && attacker.lock()->isWithinRange(building))
                        possibleDamage+= int(attacker.lock()->getGroundDamage());
                }

                return possibleDamage > building.getHealth() + building.getShields();
            };

            // Lair morphing
            if (building.getTilePosition() == BWEB::Map::getMainTile() && building.getType() == Zerg_Hatchery && !willDieToAttacks() && BuildOrder::buildCount(Zerg_Lair) > vis(Zerg_Lair) + vis(Zerg_Hive) + morphedThisFrame[Zerg_Lair] + morphedThisFrame[Zerg_Hive]) {
                building.unit()->morph(Zerg_Lair);
                morphedThisFrame[Zerg_Lair]++;
            }

            // Hive morphing
            else if (building.getTilePosition() == BWEB::Map::getMainTile() && building.getType() == Zerg_Lair && !willDieToAttacks() && BuildOrder::buildCount(Zerg_Hive) > vis(Zerg_Hive) + morphedThisFrame[Zerg_Hive]) {
                building.unit()->morph(Zerg_Hive);
                morphedThisFrame[Zerg_Hive]++;
            }

            // Greater Spire morphing
            else if (building.getType() == Zerg_Spire && !willDieToAttacks() && BuildOrder::buildCount(Zerg_Greater_Spire) > vis(Zerg_Greater_Spire) + morphedThisFrame[Zerg_Greater_Spire]) {
                building.unit()->morph(Zerg_Greater_Spire);
                morphedThisFrame[Zerg_Greater_Spire]++;
            }

            // Sunken / Spore morphing
            else if (building.getType() == Zerg_Creep_Colony && !willDieToAttacks()) {
                auto morphType = UnitTypes::None;
                auto station = BWEB::Stations::getClosestStation(building.getTilePosition());
                auto wall = BWEB::Walls::getClosestWall(building.getTilePosition());

                auto closestAttacker = Util::getClosestUnit(building.getPosition(), PlayerState::Enemy, [&](auto &u) {
                    return !u.getType().isFlyer() && !u.getType().isWorker() && u.getGroundDamage() > 0.0 && u.getSpeed() > 0.0;
                });

                // If this is a Station defense
                if (station && station->getDefenseLocations().find(building.getTilePosition()) != station->getDefenseLocations().end()) {

                    // Morph Spore if needed
                    if (Stations::needAirDefenses(*station) > morphedThisFrame[Zerg_Spore_Colony])
                        morphType = Zerg_Spore_Colony;

                    // Morph Sunken if needed
                    else if (Stations::needGroundDefenses(*station) > morphedThisFrame[Zerg_Sunken_Colony])
                        morphType = Zerg_Sunken_Colony;

                    building.unit()->morph(morphType);
                    morphedThisFrame[morphType]++;
                    queuedMineral+=morphType.mineralPrice();
                }

                // If this is a Wall defense
                if (wall && wall->getDefenses().find(building.getTilePosition()) != wall->getDefenses().end()) {

                    // Morph Sunken if needed
                    if (Walls::needGroundDefenses(*wall) > morphedThisFrame[Zerg_Sunken_Colony]) {

                        // Check timing for when an attacker will arrive
                        if (closestAttacker) {
                            auto timeToEngage = closestAttacker->getPosition().getDistance(building.getPosition()) / closestAttacker->getSpeed();
                            if (timeToEngage <= Zerg_Sunken_Colony.buildTime() || closestAttacker->getType() == Terran_Vulture)
                                morphType = Zerg_Sunken_Colony;
                        }

                        // Morph if we need defenses now
                        if (BuildOrder::makeDefensesNow())
                            morphType = Zerg_Sunken_Colony;
                    }

                    // Morph Spore if needed
                    if (Walls::needAirDefenses(*wall) > morphedThisFrame[Zerg_Spore_Colony])
                        morphType = Zerg_Spore_Colony;

                    building.unit()->morph(morphType);
                    morphedThisFrame[morphType]++;
                    queuedMineral+=morphType.mineralPrice();
                }
            }

            // Terran building needs new scv
            else if (building.getType().getRace() == Races::Terran && !building.unit()->isCompleted() && !building.unit()->getBuildUnit()) {
                auto &builder = Util::getClosestUnit(building.getPosition(), PlayerState::Self, [&](auto &u) {
                    return u.getType().isWorker() && u.getBuildType() == None;
                });

                if (builder)
                    builder->unit()->rightClick(building.unit());
            }

            // Barracks
            if (building.getType() == Terran_Barracks) {
                return;// screw lifting

                // Wall lift
                bool wallCheck = (Walls::getNaturalWall() && building.getPosition().getDistance(Walls::getNaturalWall()->getCentroid()) < 256.0)
                    || (Walls::getMainWall() && building.getPosition().getDistance(Walls::getMainWall()->getCentroid()) < 256.0);

                if (wallCheck && !building.unit()->isFlying()) {
                    if (Players::getSupply(PlayerState::Self) > 120 || BuildOrder::firstReady()) {
                        building.unit()->lift();
                        BWEB::Map::onUnitDestroy(building.unit());
                    }
                }

                // Find landing location as production building
                else if ((Players::getSupply(PlayerState::Self) > 120 || BuildOrder::firstReady()) && building.unit()->isFlying()) {
                    auto here = closestLocation(building.getType(), BWEB::Map::getMainPosition());
                    auto center = (Position)here + Position(building.getType().tileWidth() * 16, building.getType().tileHeight() * 16);

                    if (building.unit()->getLastCommand().getType() != UnitCommandTypes::Land || building.unit()->getLastCommand().getTargetTilePosition() != here)
                        building.unit()->land(here);
                }

                // Add used tiles back to grid
                else if (!building.unit()->isFlying() && building.unit()->getLastCommand().getType() == UnitCommandTypes::Land)
                    BWEB::Map::onUnitDiscover(building.unit());
            }

            // Comsat scans - Move to special manager
            if (building.getType() == Terran_Comsat_Station) {
                if (building.hasTarget() && building.getTarget().unit()->exists() && !Actions::overlapsDetection(building.unit(), building.getTarget().getPosition(), PlayerState::Enemy)) {
                    building.unit()->useTech(TechTypes::Scanner_Sweep, building.getTarget().getPosition());
                    Actions::addAction(building.unit(), building.getTarget().getPosition(), TechTypes::Scanner_Sweep, PlayerState::Self);
                }
            }

            // Cancelling Refinerys for our gas trick
            if (BuildOrder::isGasTrick() && building.getType().isRefinery() && !building.unit()->isCompleted() && BuildOrder::buildCount(building.getType()) < vis(building.getType())) {
                building.unit()->cancelMorph();
                BWEB::Map::removeUsed(building.getTilePosition(), 4, 2);
            }

            // Cancelling refineries we don't want
            if (building.getType().isRefinery() && Strategy::enemyRush() && vis(building.getType()) == 1 && building.getType().getRace() != Races::Zerg && BuildOrder::buildCount(building.getType()) < vis(building.getType())) {
                building.unit()->cancelConstruction();
            }

            // Cancelling buildings that are being built in but being attacked
            if (!building.unit()->isCompleted() && willDieToAttacks()) {
                building.unit()->cancelConstruction();
                Events::onUnitCancelBecauseBWAPISucks(building);
            }
        }

        void updateInformation(UnitInfo& building)
        {
            // If a building is unpowered, get a pylon placement ready
            if (building.getType().requiresPsi() && !Pylons::hasPower(building.getTilePosition(), building.getType())) {
                auto block = BWEB::Blocks::getClosestBlock(building.getTilePosition());
                if (block)
                    unpoweredBlocks.push_back(*block);
            }
        }

        void updateBuildings()
        {
            // Reset counters
            poweredSmall = 0; poweredMedium = 0; poweredLarge = 0;
            morphedThisFrame.clear();
            availablePieces.clear();
            unpoweredBlocks.clear();

            auto largeType = None;
            auto mediumType = None;
            auto smallType = None;

            // Protoss buildings
            if (Broodwar->self()->getRace() == Races::Protoss) {
                largeType = Protoss_Gateway;
                mediumType = Protoss_Forge;
                smallType = Protoss_Photon_Cannon;
            }
            // Terran buildings
            if (Broodwar->self()->getRace() == Races::Terran) {
                largeType = Terran_Factory;
                mediumType = Terran_Supply_Depot;
                smallType = Terran_Missile_Turret;
            }
            // Zerg buildings
            if (Broodwar->self()->getRace() == Races::Zerg) {
                largeType = Zerg_Hatchery;
                mediumType = Zerg_Evolution_Chamber;
                smallType = Zerg_Spire;
            }

            // Add up how many powered available spots we have        
            for (auto &block : BWEB::Blocks::getBlocks()) {
                for (auto &tile : block.getSmallTiles()) {
                    if (isBuildable(smallType, tile) && isQueueable(smallType, tile))
                        poweredSmall++;
                    else if (BWEB::Map::isUsed(tile) == None && Terrain::isInAllyTerritory(tile))
                        availablePieces[BWEB::Piece::Small]++;
                }
                for (auto &tile : block.getMediumTiles()) {
                    if (isBuildable(mediumType, tile) && isQueueable(mediumType, tile))
                        poweredMedium++;
                    else if (BWEB::Map::isUsed(tile) == None && Terrain::isInAllyTerritory(tile))
                        availablePieces[BWEB::Piece::Medium]++;
                }
                for (auto &tile : block.getLargeTiles()) {
                    if (isBuildable(largeType, tile) && isQueueable(largeType, tile))
                        poweredLarge++;
                    else if (BWEB::Map::isUsed(tile) == None && Terrain::isInAllyTerritory(tile))
                        availablePieces[BWEB::Piece::Large]++;
                }
            }

            // Update all my buildings
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;

                if (unit.getRole() == Role::Production || unit.getRole() == Role::Defender) {
                    updateInformation(unit);
                    updateCommands(unit);
                }
            }
        }

        void queueBuildings()
        {
            queuedMineral = 0;
            queuedGas = 0;

            // Wipe clean all queued buildings, store a cache of old buildings
            map<shared_ptr<UnitInfo>, TilePosition> oldBuilders;
            for (auto &u : Units::getUnits(PlayerState::Self)) {
                auto &unit = *u;
                if (unit.getBuildPosition().isValid()) {
                    oldBuilders[u] = unit.getBuildPosition();
                    unit.setBuildingType(UnitTypes::None);
                    unit.setBuildPosition(TilePositions::Invalid);
                }
            }
            buildingsQueued.clear();

            // Reserved resources for buildings that are trying to morph
            for (auto &[type, count] : morphedThisFrame) {
                queuedMineral += type.mineralPrice() * count;
                queuedGas += type.gasPrice() * count;
            }

            // Add up how many more buildings of each type we need
            for (auto &[building, count] : BuildOrder::getBuildQueue()) {
                int queuedCount = 0;

                auto morphed = !building.whatBuilds().first.isWorker();
                auto addon = building.isAddon();

                if (building.isAddon()
                    || !building.isBuilding())
                    continue;

                // If the building morphed from another building type, add the visible amount of child type to the parent type
                int morphOffset = 0;
                if (building == Zerg_Creep_Colony)
                    morphOffset = vis(Zerg_Sunken_Colony) + vis(Zerg_Spore_Colony);
                if (building == Zerg_Hatchery)
                    morphOffset = vis(Zerg_Lair) + vis(Zerg_Hive);
                if (building == Zerg_Lair)
                    morphOffset = vis(Zerg_Hive);

                // Queue the cost of any morphs or building
                if (count > vis(building) + morphOffset) {
                    queuedMineral += building.mineralPrice() * (count - vis(building) - morphOffset);
                    queuedGas += building.gasPrice() * (count - vis(building) - morphOffset);
                }

                if (morphed)
                    continue;

                // Queue building if our actual count is higher than our visible count
                while (count > queuedCount + vis(building) + morphOffset) {
                    queuedCount++;
                    auto here = getBuildLocation(building);
                    auto center = Position(here) + Position(building.tileWidth() * 16, building.tileHeight() * 16);

                    auto &builder = Util::getClosestUnitGround(center, PlayerState::Self, [&](auto &u) {
                        return u.getRole() == Role::Worker && !u.isStuck() && (!u.hasResource() || u.getResource().getType().isMineralField()) && u.getBuildType() == None;
                    });

                    if (builder) {
                        for (auto &[oldBuilder, oldHere] : oldBuilders) {
                            if (oldHere == here && Position(oldHere).getDistance(builder->getPosition()) > 320.0) {
                                if (oldBuilder && !oldBuilder->isStuck())
                                    builder = oldBuilder;
                            }
                        }
                    }

                    if (here.isValid() && builder && Workers::shouldMoveToBuild(*builder, here, building)) {
                        builder->setBuildingType(building);
                        builder->setBuildPosition(here);
                        buildingsQueued[here] = building;
                    }
                }
            }
        }
    }

    bool isQueueable(UnitType building, TilePosition here)
    {
        // Check if there's a building queued there already
        for (auto &queued : buildingsQueued) {
            if (queued.first == here)
                return false;
        }
        return true;
    }

    bool isBuildable(UnitType building, TilePosition here)
    {
        auto center = Position(here) + Position(building.tileWidth() * 16, building.tileHeight() * 16);

        // Check to see if we expect creep to be here soon
        auto creepSoon = false;
        if (Broodwar->self()->getRace() == Races::Zerg) {
            auto closestStation = Stations::getClosestStation(PlayerState::Self, Position(here));
            if (closestStation) {
                for (auto &[unit, station] : Stations::getMyStations()) {
                    if (station == closestStation && station->getDefenseLocations().find(here) != station->getDefenseLocations().end()) {

                        // Found that > 5 tiles away causes us to wait forever
                        if (center.getDistance(station->getBase()->Center()) > 160.0)
                            return false;

                        if (unit->getRemainingBuildTime() < 240) {
                            creepSoon = true;
                            break;
                        }
                    }
                }
            }
        }

        // Check to see if any larva overlap this spot
        if (Broodwar->self()->getRace() == Races::Zerg) {
            auto closestLarvaOrEgg = Util::getClosestUnit(center, PlayerState::Self, [&](auto &u) {
                return u.getType() == Zerg_Larva || u.getType() == Zerg_Egg;
            });

            if (closestLarvaOrEgg && overlapsUnit(*closestLarvaOrEgg, here, building))
                return false;
        }

        // Refinery only on Geysers
        if (building.isRefinery()) {
            for (auto &g : Resources::getMyGas()) {
                ResourceInfo &gas = *g;

                if (here == gas.getTilePosition() && gas.getType() != building)
                    return true;
            }
            return false;
        }

        // Used tile check
        for (int x = here.x; x < here.x + building.tileWidth(); x++) {
            for (int y = here.y; y < here.y + building.tileHeight(); y++) {
                TilePosition t(x, y);
                if (!t.isValid())
                    return false;

                if (building.getRace() == Races::Zerg && !creepSoon && building.requiresCreep() && !Broodwar->hasCreep(t))
                    return false;
                if (BWEB::Map::isUsed(t) != None)
                    return false;
            }
        }

        // Addon room check
        if (building.canBuildAddon()) {
            if (BWEB::Map::isUsed(here + TilePosition(4, 1)) != None)
                return false;
        }

        // Psi/Creep check
        if (building.requiresPsi() && !Pylons::hasPower(here, building))
            return false;
        return true;
    }

    bool overlapsQueue(UnitInfo& unit, Position here)
    {
        // Check if there's a building queued there already
        for (auto &[tile, building] : buildingsQueued) {

            if (Broodwar->self()->minerals() < building.mineralPrice()
                || Broodwar->self()->gas() < building.gasPrice()
                || unit.getBuildPosition() == tile)
                continue;

            // Bordering box of the queued building
            const auto buildingTopLeft = Position(tile);
            const auto buildingBotRight = buildingTopLeft + Position(building.tileSize());

            // Bordering box of the unit
            const auto unitTopLeft = here + Position(-unit.getType().width() / 2, -unit.getType().height() / 2);
            const auto unitTopRight = here + Position(unit.getType().width() / 2, -unit.getType().height() / 2);
            const auto unitBotLeft = here + Position(-unit.getType().width() / 2, unit.getType().height() / 2);
            const auto unitBotRight = here + Position(unit.getType().width() / 2, unit.getType().height() / 2);

            return (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitTopLeft))
                || (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitTopRight))
                || (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitBotLeft))
                || (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitBotRight));

        }
        return false;
    }

    bool overlapsUnit(UnitInfo& unit, TilePosition here, UnitType building)
    {
        // Bordering box of the queued building
        const auto buildingTopLeft = Position(here);
        const auto buildingBotRight = buildingTopLeft + Position(building.tileSize());

        // Bordering box of the unit
        const auto unitTopLeft = unit.getPosition() + Position(-unit.getType().width() / 2, -unit.getType().height() / 2);
        const auto unitTopRight = unit.getPosition() + Position(unit.getType().width() / 2, -unit.getType().height() / 2);
        const auto unitBotLeft = unit.getPosition() + Position(-unit.getType().width() / 2, unit.getType().height() / 2);
        const auto unitBotRight = unit.getPosition() + Position(unit.getType().width() / 2, unit.getType().height() / 2);

        return (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitTopLeft))
            || (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitTopRight))
            || (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitBotLeft))
            || (Util::rectangleIntersect(buildingTopLeft, buildingBotRight, unitBotRight));
    }

    bool hasPoweredPositions() { return (poweredLarge > 0 && poweredMedium > 0); }
    int getQueuedMineral() { return queuedMineral; }
    int getQueuedGas() { return queuedGas; }
    TilePosition getCurrentExpansion() { return currentExpansion; }

    void onFrame()
    {
        updateBuildings();
        queueBuildings();

        TilePosition test;
        testResourceDepotSpots(test, None);
    }

    void onStart()
    {
        // Initialize Blocks
        BWEB::Blocks::findBlocks();
    }
}