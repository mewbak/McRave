#include "McRave.h"

void SpecialUnitTrackerClass::update()
{
	updateArbiters();
	updateObservers();
	updateTemplars();
	updateReavers();
	return;	
}

void SpecialUnitTrackerClass::updateArbiters()
{
	for (auto & u : myArbiters)
	{
		// Move towards high cluster counts and closest to ally starting position
		int bestCluster = 0;
		double closestD = 0.0;
		Position bestPosition = GridTracker::Instance().getArmyCenter();
		WalkPosition start = u.second.getMiniTile();
		for (int x = start.x - 20; x <= start.x + 20; x++)
		{
			for (int y = start.y - 20; y <= start.y + 20; y++)
			{
				if (WalkPosition(x, y).isValid() && GridTracker::Instance().getArbiterGrid(x, y) == 0 && GridTracker::Instance().getEAirGrid(x, y) == 0.0 && (closestD == 0.0 || GridTracker::Instance().getACluster(x, y) > bestCluster || (GridTracker::Instance().getACluster(x, y) == bestCluster && TerrainTracker::Instance().getPlayerStartingPosition().getDistance(Position(WalkPosition(x, y))) < closestD)))
				{
					closestD = TerrainTracker::Instance().getPlayerStartingPosition().getDistance(Position(WalkPosition(x, y)));
					bestCluster = GridTracker::Instance().getACluster(x, y);
					bestPosition = Position(WalkPosition(x, y));
				}
				if (WalkPosition(x, y).isValid())
				{
					Broodwar->drawBoxMap(Position(x * 8, y * 8), Position(x * 8 + 8, y * 8 + 8), Broodwar->self()->getColor());
				}
			}
		}
		// Move and update grids	
		u.second.setDestination(bestPosition);
		u.first->move(bestPosition);
		GridTracker::Instance().updateArbiterMovement(u.first);

		// If there's a stasis target, cast stasis on it
		Unit target = UnitTracker::Instance().getMyUnits()[u.first].getTarget();
		if (target && target->exists() && u.first->getEnergy() >= 100)
		{
			u.first->useTech(TechTypes::Stasis_Field, target);
			Broodwar->drawLineMap(u.second.getPosition(), target->getPosition(), Broodwar->self()->getColor());
		}
		else
		{
			Broodwar->drawLineMap(u.second.getPosition(), u.second.getDestination(), Broodwar->self()->getColor());
		}
	}
	return;
}

void SpecialUnitTrackerClass::updateObservers()
{
	for (auto & u : myObservers)
	{
		// First check if any expansions need detection on them
		if (BuildOrderTracker::Instance().getBuildingDesired()[UnitTypes::Protoss_Nexus] > Broodwar->self()->completedUnitCount(UnitTypes::Protoss_Nexus))
		{
			bool baseScout = false;
			for (auto base : TerrainTracker::Instance().getNextExpansion())
			{
				// If an expansion is unbuildable and we've scouted it already, move there to detect burrowed units
				if (!Broodwar->canBuildHere(base, UnitTypes::Protoss_Nexus, nullptr) && GridTracker::Instance().getNexusGrid(base.x, base.y) == 0)
				{
					u.second.setDestination(Position(base));
					u.first->move(Position(base));
					GridTracker::Instance().updateObserverMovement(u.first);
					baseScout = true;
				}
			}
			if (baseScout)
			{
				continue;
			}
		}

		// Move towards lowest enemy air threat, no enemy detection and closest to enemy starting position	
		double closestD = 0.0;
		Position newDestination = GridTracker::Instance().getArmyCenter();
		WalkPosition start = u.second.getMiniTile();
		for (int x = start.x - 20; x <= start.x + 20; x++)
		{
			for (int y = start.y - 20; y <= start.y + 20; y++)
			{
				if (WalkPosition(x, y).isValid() && GridTracker::Instance().getEDetectorGrid(x, y) == 0 && Position(WalkPosition(x, y)).getDistance(Position(start)) > 64 && GridTracker::Instance().getACluster(x, y) > 0 && GridTracker::Instance().getObserverGrid(x, y) == 0 && GridTracker::Instance().getEAirGrid(x, y) == 0.0 && (Position(WalkPosition(x, y)).getDistance(TerrainTracker::Instance().getEnemyStartingPosition()) < closestD || closestD == 0))
				{
					newDestination = Position(WalkPosition(x, y));
					closestD = Position(WalkPosition(x, y)).getDistance(TerrainTracker::Instance().getEnemyStartingPosition());
				}
			}
		}
		u.second.setDestination(newDestination);
		u.first->move(newDestination);
		GridTracker::Instance().updateObserverMovement(u.first);
		Broodwar->drawLineMap(u.second.getPosition(), u.second.getDestination(), Broodwar->self()->getColor());
		continue;
	}
	return;
}

void SpecialUnitTrackerClass::updateTemplars()
{
	for (auto & u : myTemplars)
	{
		// If we should warp an archon
		if (u.first->isUnderAttack() && u.first->getClosestUnit(Filter::IsAlly && Filter::GetType == UnitTypes::Protoss_High_Templar))
		{
			if (!u.first->getLastCommand().getType() == UnitCommandTypes::Use_Tech_Unit)
			{
				u.first->useTech(TechTypes::Archon_Warp, u.first->getClosestUnit(Filter::IsAlly && Filter::GetType == UnitTypes::Protoss_High_Templar));
			}
			continue;
		}
	}
	return;
}

void SpecialUnitTrackerClass::updateReavers()
{
	for (auto & u : myReavers)
	{
		if (u.first->getScarabCount() < 5)
		{
			u.first->train(UnitTypes::Protoss_Scarab);
		}
	}
}

void SpecialUnitTrackerClass::storeUnit(Unit unit)
{
	if (unit->getType() == UnitTypes::Protoss_Arbiter)
	{
		myArbiters[unit].setPosition(unit->getPosition());
		myArbiters[unit].setDestination(unit->getPosition());
		myArbiters[unit].setMiniTile(UnitUtil::Instance().getMiniTile(unit));
	}
	else if (unit->getType() == UnitTypes::Protoss_Observer)
	{
		myObservers[unit].setPosition(unit->getPosition());
		myObservers[unit].setDestination(unit->getPosition());
		myObservers[unit].setMiniTile(UnitUtil::Instance().getMiniTile(unit));
	}
	else if (unit->getType() == UnitTypes::Protoss_High_Templar)
	{
		myTemplars[unit].setPosition(unit->getPosition());
		myTemplars[unit].setDestination(unit->getPosition());
		myTemplars[unit].setMiniTile(UnitUtil::Instance().getMiniTile(unit));
	}
	else if (unit->getType() == UnitTypes::Protoss_Reaver)
	{
		myReavers[unit].setPosition(unit->getPosition());
		myReavers[unit].setDestination(unit->getPosition());
		myReavers[unit].setMiniTile(UnitUtil::Instance().getMiniTile(unit));
	}	
	return;
}

void SpecialUnitTrackerClass::removeUnit(Unit unit)
{
	if (myArbiters.find(unit) != myArbiters.end())
	{
		myArbiters.erase(unit);
	}
	else if (myObservers.find(unit) != myObservers.end())
	{
		myObservers.erase(unit);
	}
	else if (myTemplars.find(unit) != myTemplars.end())
	{
		myTemplars.erase(unit);
	}
	else if (myReavers.find(unit) != myReavers.end())
	{
		myReavers.erase(unit);
	}	
	return;
}
