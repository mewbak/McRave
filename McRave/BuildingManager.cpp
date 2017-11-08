#include "McRave.h"

void BuildingTrackerClass::update()
{
	Display().startClock();
	updateBuildings();
	queueBuildings();
	constructBuildings();
	Display().performanceTest(__FUNCTION__);
	return;
}

void BuildingTrackerClass::updateBuildings()
{
	for (auto& b : myBuildings)
	{
		BuildingInfo building = b.second;
		building.setIdleStatus(building.unit()->getRemainingTrainTime() == 0);
		building.setEnergy(building.unit()->getEnergy());

		if (b.second.getType().getRace() == Races::Terran && !building.unit()->isCompleted() && !building.unit()->getBuildUnit())
		{
			Unit builder = Workers().getClosestWorker(building.getPosition());
			if (builder)
			{
				builder->rightClick(building.unit());
			}
			continue;
		}
	}
	return;
}

void BuildingTrackerClass::queueBuildings()
{
	// For each building desired
	for (auto &b : BuildOrder().getBuildingDesired())
	{		
		int cnt = 0;
		for (auto &queued : buildingsQueued)
		{
			if (queued.second == b.first) cnt++;
		}

		// If our visible count is lower than our desired count
		if (b.second > Broodwar->self()->visibleUnitCount(b.first) && b.second - Broodwar->self()->visibleUnitCount(b.first) > cnt)
		{
			TilePosition here = Buildings().getBuildLocation(b.first);
			Unit builder = Workers().getClosestWorker(Position(here));

			// If the Tile Position and Builder are valid
			if (here.isValid() && builder)
			{
				// Queue at this building type a pair of building placement and builder	
				Workers().getMyWorkers()[builder].setBuildingType(b.first);
				Workers().getMyWorkers()[builder].setBuildPosition(here);
			}
		}
	}
}

void BuildingTrackerClass::constructBuildings()
{
	int offset = 200;
	queuedMineral = 0;
	queuedGas = 0;
	buildingsQueued.clear();
	for (auto &worker : Workers().getMyWorkers())
	{
		if (worker.second.getBuildingType().isValid() && worker.second.getBuildPosition().isValid())
		{
			buildingsQueued[worker.second.getBuildPosition()] = worker.second.getBuildingType();
			queuedMineral += worker.second.getBuildingType().mineralPrice();
			queuedGas += worker.second.getBuildingType().gasPrice();
			Broodwar->drawTextScreen(0, offset, "%s", worker.second.getBuildingType().c_str());
			offset += 10;
		}
	}
}

void BuildingTrackerClass::storeBuilding(Unit building)
{
	BuildingInfo &b = myBuildings[building];
	b.setUnit(building);
	b.setType(building->getType());
	b.setEnergy(building->getEnergy());
	b.setPosition(building->getPosition());
	b.setWalkPosition(Util().getWalkPosition(building));
	b.setTilePosition(building->getTilePosition());
	Grids().updateBuildingGrid(b);
	return;
}

void BuildingTrackerClass::removeBuilding(Unit building)
{
	Grids().updateBuildingGrid(myBuildings[building]);
	myBuildings.erase(building);
	return;
}

TilePosition BuildingTrackerClass::getBuildLocationNear(UnitType building, TilePosition buildTilePosition)
{
	int x = buildTilePosition.x;
	int y = buildTilePosition.y;
	int length = 1, j = 0, dx = 0, dy = 1;
	bool first = true;

	// Searches in a spiral around the specified tile position
	while (length < 50)
	{
		// If we can build here, return this tile position		
		if (TilePosition(x, y).isValid() && isBuildable(building, TilePosition(x, y)) && isQueueable(building, TilePosition(x, y)) && isSuitable(building, TilePosition(x, y))) return TilePosition(x, y);

		// Otherwise spiral out and find a new tile
		x = x + dx;
		y = y + dy;
		j++;
		if (j == length)
		{
			j = 0;
			if (!first) length++;
			first = !first;
			if (dx == 0) dx = dy, dy = 0;
			else dy = -dx, dx = 0;
		}
	}
	if (Broodwar->getFrameCount() - errorTime > 500)
	{
		Broodwar << "Issues placing a " << building.c_str() << endl;
		errorTime = Broodwar->getFrameCount();
	}
	return TilePositions::Invalid;
}

TilePosition BuildingTrackerClass::getBuildLocation(UnitType building)
{
	// Refineries are only built on my own gas resources
	if (building.isRefinery())
	{
		for (auto &gas : Resources().getMyGas())
		{
			if (Grids().getBaseGrid(gas.second.getTilePosition()) > 0 && gas.second.getType() == UnitTypes::Resource_Vespene_Geyser) return gas.second.getTilePosition();			
		}
		return TilePositions::Invalid;
	}

	// If we are expanding, it must be on an expansion area
	double closestD = 0.0;
	TilePosition closestP, here = TilePositions::Invalid;
	if (building.isResourceDepot())
	{
		// Fast expands must be as close to home and have a gas geyser
		if (Broodwar->self()->visibleUnitCount(UnitTypes::Protoss_Nexus) == 1)
		{
			for (auto &area : theMap.Areas())
			{
				for (auto &base : area.Bases())
				{
					if ((base.Geysers().size() == 0) || area.AccessibleNeighbours().size() == 0)
					{
						continue;
					}
					if (Grids().getBaseGrid(base.Location()) == 0 && (Grids().getDistanceHome(WalkPosition(base.Location())) < closestD || closestD == 0.0))
					{
						closestD = Grids().getDistanceHome(WalkPosition(base.Location()));
						closestP = base.Location();
					}
				}
			}
		}

		// Other expansions must be as close to home but as far away from the opponent
		else
		{
			for (auto &area : theMap.Areas())
			{
				for (auto &base : area.Bases())
				{
					if (area.AccessibleNeighbours().size() == 0 || base.Center() == Terrain().getEnemyStartingPosition())
					{
						continue;
					}
					if (Grids().getBuildingGrid(base.Location()) == 0 && (Grids().getDistanceHome(WalkPosition(base.Location())) / base.Center().getDistance(Terrain().getEnemyStartingPosition()) < closestD || closestD == 0.0))
					{
						closestD = Grids().getDistanceHome(WalkPosition(base.Location())) / base.Center().getDistance(Terrain().getEnemyStartingPosition());
						closestP = base.Location();
					}
				}
			}
		}
		currentExpansion = closestP;
		return closestP;
	}

	// If we are doing nexus first
	if (BuildOrder().isOpener() && BuildOrder().isNexusFirst() && ((building == UnitTypes::Protoss_Gateway && Broodwar->self()->visibleUnitCount(UnitTypes::Protoss_Gateway) <= 1) || building == UnitTypes::Protoss_Cybernetics_Core || (building == UnitTypes::Protoss_Pylon && Grids().getPylonGrid(Terrain().getSecondChoke()) <= 0)))
	{
		here = getBuildLocationNear(building, Terrain().getSecondChoke());
		if (!here.isValid()) here = getBuildLocationNear(building, Terrain().getPlayerStartingTilePosition());
		return here;
	}

	// If we are forge expanding
	if (BuildOrder().isOpener() && BuildOrder().isForgeExpand() && (building == UnitTypes::Protoss_Photon_Cannon || (building == UnitTypes::Protoss_Pylon && Grids().getPylonGrid(Terrain().getFFEPosition()) <= 0) || (building == UnitTypes::Protoss_Gateway && Broodwar->self()->visibleUnitCount(UnitTypes::Protoss_Gateway) <= 0) || (building == UnitTypes::Protoss_Forge && Broodwar->self()->visibleUnitCount(UnitTypes::Protoss_Forge) <= 0)))
	{
		here = getBuildLocationNear(building, Terrain().getFFEPosition());
		if (!here.isValid()) here = getBuildLocationNear(building, Terrain().getPlayerStartingTilePosition());
		return here;
	}

	// If we are being rushed and need a battery
	if (building == UnitTypes::Protoss_Shield_Battery)
	{
		if (Strategy().isAllyFastExpand()) here = getBuildLocationNear(building, Terrain().getSecondChoke());
		else here = getBuildLocationNear(building, Terrain().getFirstChoke());
		if (!here.isValid()) here = getBuildLocationNear(building, Terrain().getPlayerStartingTilePosition());
		return here;
	}


	// If it's a pylon, check if any bases need a pylon
	if (building == UnitTypes::Protoss_Pylon && Broodwar->self()->completedUnitCount(UnitTypes::Protoss_Nexus) >= 2)
	{
		for (auto &base : Bases().getMyBases())
		{
			if (Grids().getPylonGrid(base.second.getTilePosition()) == 0) return getBuildLocationNear(building, base.second.getTilePosition());			
		}
	}

	if (building == UnitTypes::Protoss_Photon_Cannon)
	{
		for (auto &base : Bases().getMyBases())
		{
			if (Grids().getDefenseGrid(base.second.getTilePosition()) <= 0 && Grids().getPylonGrid(base.second.getTilePosition()) > 0) return getBuildLocationNear(building, base.second.getTilePosition());			
		}
		return TilePositions::Invalid;
	}

	// For each base, check if you can build near it, starting at the main
	for (auto &base : Bases().getMyOrderedBases())
	{
		here = getBuildLocationNear(building, base.second);
		if (here.isValid()) return here;		
	}
	return TilePositions::Invalid;
}

bool BuildingTrackerClass::isQueueable(UnitType building, TilePosition buildTilePosition)
{
	// Check if there's a building queued there already
	for (auto &queued : buildingsQueued)
	{
		if (buildTilePosition.x < queued.first.x + queued.second.tileWidth() && queued.first.x < buildTilePosition.x + building.tileWidth() && buildTilePosition.y < queued.first.y + queued.second.tileHeight() && queued.first.y < buildTilePosition.y + building.tileHeight())
		{
			return false;
		}
	}
	return true;
}

bool BuildingTrackerClass::isBuildable(UnitType building, TilePosition buildTilePosition)
{
	if (building.isRefinery()) return true;
	if (Broodwar->self()->getRace() == Races::Protoss && building.requiresPsi() && !Pylons().hasPower(buildTilePosition, building)) return false; // If Protoss, check if it's not a pylon and in a preset buildable position based on power grid
	if (building == UnitTypes::Protoss_Shield_Battery && Terrain().getMineralHoldPosition().getDistance(Position(buildTilePosition)) > 160) return false;

	for (int x = buildTilePosition.x; x < buildTilePosition.x + building.tileWidth(); x++)
	{
		for (int y = buildTilePosition.y; y < buildTilePosition.y + building.tileHeight(); y++)
		{
			if (!TilePosition(x, y).isValid()) return false;
			if (!Broodwar->isBuildable(TilePosition(x, y), true)) return false; // If it's on an unbuildable tile
			if (Grids().getBuildingGrid(x, y) > 0 && !building.isResourceDepot()) return false; // If it's reserved for expansions				
			if (building == UnitTypes::Protoss_Pylon && Grids().getPylonGrid(x, y) >= 2) return false; // If it's a pylon and overlapping too many pylons				
			if (!building.isResourceDepot() && building != UnitTypes::Protoss_Photon_Cannon && building != UnitTypes::Protoss_Shield_Battery && building != UnitTypes::Terran_Bunker && Grids().getResourceGrid(x, y) > 0) return false; // If it's not a defensive structure and on top of the resource grid
		}
	}

	// If the building is not a resource depot and being placed on an expansion
	if (!building.isResourceDepot())
	{
		for (auto &base : Terrain().getAllBaseLocations())
		{
			for (int i = 0; i < building.tileWidth(); i++)
			{
				for (int j = 0; j < building.tileHeight(); j++)
				{
					// If the x value of this tile of the building is within an expansion and the y value of this tile of the building is within an expansion, return false
					if (buildTilePosition.x + i >= base.x && buildTilePosition.x + i < base.x + 4 && buildTilePosition.y + j >= base.y && buildTilePosition.y + j < base.y + 3)
					{
						return false;
					}
				}
			}
		}
	}

	// If the building can build addons
	if (building.canBuildAddon())
	{
		for (int x = buildTilePosition.x + building.tileWidth(); x <= buildTilePosition.x + building.tileWidth() + 2; x++)
		{
			for (int y = buildTilePosition.y + 1; y <= buildTilePosition.y + 3; y++)
			{
				if (Grids().getBuildingGrid(x, y) > 0 || !Broodwar->isBuildable(TilePosition(x, y), true))
				{
					return false;
				}
			}
		}
	}

	// If no issues, return true
	return true;
}

bool BuildingTrackerClass::isSuitable(UnitType building, TilePosition buildTilePosition)
{
	// Production buildings that create ground units require spacing so they don't trap units -- TEMP: Supply depot to not block SCVs (need to find solution)
	if (building == UnitTypes::Terran_Supply_Depot || (!building.isResourceDepot() && building.buildsWhat().size() > 0)) buildingOffset = 1;
	else buildingOffset = 0;

	if (Strategy().isAllyFastExpand() && ((building == UnitTypes::Protoss_Gateway && Broodwar->self()->visibleUnitCount(UnitTypes::Protoss_Gateway) <= 0) || (building == UnitTypes::Protoss_Forge && Broodwar->self()->visibleUnitCount(UnitTypes::Protoss_Forge) <= 0)))
	{
		bool validFFE = false;
		if (building == UnitTypes::Protoss_Gateway)
		{
			int dx = buildTilePosition.x + building.tileWidth();
			for (int y = buildTilePosition.y; y < buildTilePosition.y + building.tileHeight(); y++)
			{
				if (!Broodwar->isBuildable(TilePosition(dx, y))) validFFE = true;
			}

			int dy = buildTilePosition.y + building.tileHeight();
			for (int x = buildTilePosition.x; x < buildTilePosition.x + building.tileWidth(); x++)
			{
				if (!Broodwar->isBuildable(TilePosition(x, dy))) validFFE = true;
			}
		}
		if (building == UnitTypes::Protoss_Forge)
		{
			int dx = buildTilePosition.x - 1;
			for (int y = buildTilePosition.y; y < buildTilePosition.y + building.tileHeight(); y++)
			{
				if (!Broodwar->isBuildable(TilePosition(dx, y))) validFFE = true;
			}

			int dy = buildTilePosition.y - 1;
			for (int x = buildTilePosition.x; x < buildTilePosition.x + building.tileWidth(); x++)
			{
				if (!Broodwar->isBuildable(TilePosition(x, dy))) validFFE = true;
			}
		}
		if (!validFFE) return false;
	}

	// If the building requires an offset (production buildings and first pylon)
	else if (buildingOffset > 0)
	{
		for (int x = buildTilePosition.x - buildingOffset; x < buildTilePosition.x + building.tileWidth() + buildingOffset; x++)
		{
			for (int y = buildTilePosition.y - buildingOffset; y < buildTilePosition.y + building.tileHeight() + buildingOffset; y++)
			{
				if (!TilePosition(x, y).isValid())
				{
					return false;
				}
				if (Grids().getBuildingGrid(x, y) > 0 && !Broodwar->isBuildable(TilePosition(x, y), true))
				{
					return false;
				}
				if (building == UnitTypes::Protoss_Pylon && !Broodwar->isBuildable(TilePosition(x, y), true))
				{
					return false;
				}
			}
		}
	}
	return true;
}

set<Unit> BuildingTrackerClass::getAllyBuildingsFilter(UnitType type)
{
	returnValues.clear();
	for (auto &u : myBuildings)
	{
		BuildingInfo &unit = u.second;
		if (unit.getType() == type)
		{
			returnValues.insert(unit.unit());
		}
	}
	return returnValues;
}

BuildingInfo& BuildingTrackerClass::getAllyBuilding(Unit building)
{
	if (myBuildings.find(building) != myBuildings.end()) return myBuildings[building];
	assert();
	return BuildingInfo();
}