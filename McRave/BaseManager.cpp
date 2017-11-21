#include "McRave.h"

void BaseTrackerClass::update()
{
	Display().startClock();
	updateBases();
	Display().performanceTest(__FUNCTION__);
	return;
}

void BaseTrackerClass::updateBases()
{
	for (auto &b : myBases)
	{
		BaseInfo& base = b.second;
		if (!base.unit() || !base.unit()->exists()) continue;
		updateProduction(base);

		if (Grids().getBaseGrid(base.getTilePosition()) == 1 && base.unit()->isCompleted())
			Grids().updateBaseGrid(base);
	}
	for (auto &b : enemyBases)
	{
		BaseInfo& base = b.second;
		if (!base.unit()) continue;
		if (base.unit()->exists())
		{
			base.setLastVisibleFrame(Broodwar->getFrameCount());
		}
		if (!base.unit()->exists() && Broodwar->isVisible(base.getTilePosition()))
		{
			enemyBases.erase(base.unit());
			break;
		}
	}
	return;
}

void BaseTrackerClass::updateProduction(BaseInfo& base)
{
	if (base.getType() == UnitTypes::Terran_Command_Center && !base.unit()->getAddon())
	{
		base.unit()->buildAddon(UnitTypes::Terran_Comsat_Station);
	}

	if (base.unit() && base.unit()->isIdle() && ((!Resources().isMinSaturated() || !Resources().isGasSaturated()) || BuildOrder().getCurrentBuild() == "Sparks"))
	{
		for (auto &unit : base.getType().buildsWhat())
		{
			if (unit.isWorker())
			{
				if (Broodwar->self()->completedUnitCount(unit) < 75 && (Broodwar->self()->minerals() >= unit.mineralPrice() + Production().getReservedMineral() + Buildings().getQueuedMineral()))
				{
					base.unit()->train(unit);
				}
			}
		}
		for (auto &unit : base.unit()->getLarva())
		{
			if (Broodwar->self()->completedUnitCount(UnitTypes::Zerg_Drone) < 60 && (Broodwar->self()->minerals() >= UnitTypes::Zerg_Drone.mineralPrice() + Production().getReservedMineral() + Buildings().getQueuedMineral()))
			{
				base.unit()->morph(UnitTypes::Zerg_Drone);
			}
		}
	}
	return;
}

Position BaseTrackerClass::getClosestEnemyBase(Position here)
{
	double closestD = 0.0;
	Position closestP;
	for (auto &b : enemyBases)
	{
		BaseInfo& base = b.second;
		if (here.getDistance(base.getPosition()) < closestD || closestD == 0.0)
		{
			closestP = base.getPosition();
			closestD = here.getDistance(base.getPosition());
		}
	}
	return closestP;
}

void BaseTrackerClass::storeBase(Unit base)
{
	BaseInfo& b = base->getPlayer() == Broodwar->self() ? myBases[base] : enemyBases[base];
	b.setUnit(base);
	b.setType(base->getType());
	b.setResourcesPosition(TilePosition(Resources().resourceClusterCenter(base)));
	b.setPosition(base->getPosition());
	b.setWalkPosition(Util().getWalkPosition(base));
	b.setTilePosition(base->getTilePosition());
	b.setPosition(base->getPosition());

	if (b.getTilePosition().isValid() && theMap.GetArea(b.getTilePosition()))
	{
		if (base->getPlayer() == Broodwar->self())
		{
			myOrderedBases[base->getPosition().getDistance(Terrain().getPlayerStartingPosition())] = base->getTilePosition();
			Terrain().getAllyTerritory().insert(theMap.GetArea(b.getTilePosition())->Id());
			Grids().updateBaseGrid(b);
		}
		else
		{
			Terrain().getEnemyTerritory().insert(theMap.GetArea(b.getTilePosition())->Id());
		}
	}

	if (base->getPlayer() == Broodwar->self())
	{
		for (auto r : Resources().getMyMinerals())
		{
			ResourceInfo &resource = r.second;
			if (resource.getClosestBasePosition() == base->getPosition())
			{
				Grids().updateResourceGrid(resource);
			}
		}
	}
	return;
}

void BaseTrackerClass::removeBase(Unit base)
{
	if (base->getPlayer() == Broodwar->self())
	{
		Terrain().getAllyTerritory().erase(theMap.GetArea(base->getTilePosition())->Id());
		Grids().updateBaseGrid(myBases[base]);
		myOrderedBases.erase(base->getPosition().getDistance(Terrain().getPlayerStartingPosition()));
		myBases.erase(base);
	}
	else
	{
		Terrain().getEnemyTerritory().erase(theMap.GetArea(base->getTilePosition())->Id());
		enemyBases.erase(base);
	}

	if (base->getPlayer() == Broodwar->self())
	{
		for (auto r : Resources().getMyMinerals())
		{
			ResourceInfo &resource = r.second;
			if (resource.getClosestBasePosition() == base->getPosition())
			{
				Grids().updateResourceGrid(resource);
			}
		}
	}
	return;
}