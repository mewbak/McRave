#include "McRave.h"

void BaseTrackerClass::update()
{
	for (auto &base : myBases)
	{		
		trainWorkers(base.second);
		updateDefenses(base.second);
	}
	return;
}

void BaseTrackerClass::storeBase(Unit base)
{
	if (myBases.find(base) == myBases.end())
	{
		myBases[base].setUnit(base);
		myBases[base].setUnitType(base->getType());
		myBases[base].setResourcesPosition(centerOfResources(base));
		myBases[base].setPosition(base->getPosition());
		myBases[base].setWalkPosition(Util().getWalkPosition(base));
		myBases[base].setTilePosition(base->getTilePosition());		
		myBases[base].setPosition(base->getPosition());		
	}	

	if (Grids().isAnalyzed())
	{
		myBases[base].setRegion(getRegion(myBases[base].getTilePosition()));	
		myOrderedBases[base->getPosition().getDistance(Terrain().getPlayerStartingPosition())] = base->getTilePosition();
	}	
	return;
}

void BaseTrackerClass::removeBase(Unit base)
{
	if (myBases.find(base) != myBases.end())
	{		
		myBases.erase(base);
	}
	return;
}

void BaseTrackerClass::trainWorkers(BaseInfo& base)
{
	if (base.unit() && (!Resources().isMinSaturated() || !Resources().isGasSaturated()) && base.unit()->isIdle())
	{
		if (base.getUnitType() == UnitTypes::Protoss_Nexus && Broodwar->self()->allUnitCount(UnitTypes::Protoss_Probe) < 60 && (Broodwar->self()->minerals() >= UnitTypes::Protoss_Probe.mineralPrice() + Production().getReservedMineral() + Buildings().getQueuedMineral()))
		{
			base.unit()->train(UnitTypes::Protoss_Probe);
		}
		else if (base.getUnitType() == UnitTypes::Terran_Command_Center && Broodwar->self()->allUnitCount(UnitTypes::Terran_SCV) < 60 && (Broodwar->self()->minerals() >= UnitTypes::Terran_SCV.mineralPrice() + Production().getReservedMineral() + Buildings().getQueuedMineral()))
		{
			base.unit()->train(UnitTypes::Terran_SCV);
		}
	}
	return;
}

void BaseTrackerClass::updateDefenses(BaseInfo& base)
{
	if (Terrain().isAnalyzed())
	{
		Terrain().getAllyTerritory().emplace(theMap.GetArea(base.getTilePosition())->Id());
	}
	return;
}

TilePosition BaseTrackerClass::centerOfResources(Unit base)
{
	// Get average of minerals	
	int avgX = 0, avgY = 0, size = 0;
	for (auto &m : Broodwar->getUnitsInRadius(base->getPosition(), 320, Filter::IsMineralField))
	{
		avgX = avgX + m->getTilePosition().x;
		avgY = avgY + m->getTilePosition().y;
		size++;
	}
	if (size == 0)
	{
		return TilePositions::None;
	}

	avgX = avgX / size;
	avgY = avgY / size;

	return TilePosition(avgX, avgY);
}