#include "CommandManager.h"
#include "GridManager.h"
#include "StrategyManager.h"
#include "TerrainManager.h"
#include "UnitManager.h"

void CommandTrackerClass::update()
{
	for (auto &u : UnitTracker::Instance().getMyUnits())
	{		
		// Latency returning for now, else make a decision
		if (Broodwar->getFrameCount() % Broodwar->getLatencyFrames() != 0)
		{
			return;
		}
		updateDecisions(u.first, u.second.getTarget());
		updateLocalStrategy(u.first, u.second.getTarget());
		updateGlobalStrategy(u.first, u.second.getTarget());
	}
}

void CommandTrackerClass::updateDecisions(Unit unit, Unit target)
{
	double closestD = 0.0;
	Position closestP;


	// Ignore the unit if it no longer exists, is locked down, maelstrommed, stassised, not powered or not completed
	if (!unit || !unit->exists() || unit->isLockedDown() || unit->isMaelstrommed() || unit->isStasised() || !unit->isPowered() || !unit->isCompleted())
	{
		return;
	}
	if (!target || target == nullptr)
	{
		return;
	}	

	// If Reaver, train scarabs
	if (unit->getType() == UnitTypes::Protoss_Reaver && unit->getScarabCount() < 5)
	{
		unit->train(UnitTypes::Protoss_Scarab);
	}
	
	// Attack
	if (UnitTracker::Instance().getMyUnits()[unit].getStrategy() == 1 && target->exists())
	{
		unitMicroTarget(unit, target);
		return;
	}
	// Retreat
	if (UnitTracker::Instance().getMyUnits()[unit].getStrategy() == 0)
	{
		// Force engage Zealots on ramp
		if (TerrainTracker::Instance().getAllyTerritory().size() <= 1 && unit->getDistance(TerrainTracker::Instance().getDefendHere().at(0)) < 64 && unit->getType() == UnitTypes::Protoss_Zealot && unit->getUnitsInRadius(64, Filter::IsEnemy).size() > 0)
		{
			unitMicroTarget(unit, target);
			return;
		}

		// Create concave when containing units
		if (GridTracker::Instance().getEGroundGrid(unit->getTilePosition().x, unit->getTilePosition().y) == 0.0 && globalStrategy == 1)
		{
			Position fleePosition = unitFlee(unit, target);
			if (fleePosition != Positions::None)
			{
				unit->move(Position(fleePosition.x + rand() % 3 + (-1), fleePosition.y + rand() % 3 + (-1)));
			}
			return;
		}

		// For each defensive position, find closest one		
		for (auto position : TerrainTracker::Instance().getDefendHere())
		{
			if (unit->getDistance(position) < 320 || TerrainTracker::Instance().getAllyTerritory().find(getRegion(unit->getTilePosition())) != TerrainTracker::Instance().getAllyTerritory().end())
			{
				Position fleePosition = unitFlee(unit, target);
				if (fleePosition != Positions::None)
				{
					unit->move(Position(fleePosition.x + rand() % 3 + (-1), fleePosition.y + rand() % 3 + (-1)));
				}
				return;
			}
			if (unit->getDistance(position) <= closestD || closestD == 0.0)
			{
				closestD = unit->getDistance(position);
				closestP = position;
			}
		}

		// If last command was too far away from this position, move there
		if (unit->getLastCommand().getTargetPosition().getDistance(TerrainTracker::Instance().getDefendHere().at(0)) > 5)
		{
			unit->move(Position(TerrainTracker::Instance().getDefendHere().at(0).x + rand() % 3 + (-1), TerrainTracker::Instance().getDefendHere().at(0).y + rand() % 3 + (-1)));
		}
		return;
	}


	if (globalStrategy == 0)
	{
		if (TerrainTracker::Instance().getEnemyBasePositions().size() > 0 && TerrainTracker::Instance().getAllyTerritory().size() > 0)
		{
			// Pick random enemy bases to attack (cap at ~3-4 units?)
			closestD = 1000.0;
			closestP = TerrainTracker::Instance().getDefendHere().at(0);

			// If we forced to expand, move to next choke to prevent blocking 
			if (StrategyTracker::Instance().isFastExpand())
			{
				closestP = Position(TerrainTracker::Instance().getPath().at(1)->Center());
				if (TerrainTracker::Instance().getAllyTerritory().find(getNearestChokepoint(TilePosition(TerrainTracker::Instance().getPath().at(1)->Center()))->getRegions().second) != TerrainTracker::Instance().getAllyTerritory().end() || TerrainTracker::Instance().getAllyTerritory().find(getNearestChokepoint(TilePosition(TerrainTracker::Instance().getPath().at(1)->Center()))->getRegions().first) != TerrainTracker::Instance().getAllyTerritory().end())
				{
					closestP = Position(TerrainTracker::Instance().getPath().at(2)->Center());
				}
			}
			else
			{
				// Check if we are close enough to any defensive position
				for (auto position : TerrainTracker::Instance().getDefendHere())
				{
					if (unit->getDistance(position) < 128)
					{
						closestP = position;
						break;
					}
					else if (unit->getDistance(position) <= closestD)
					{
						closestD = unit->getDistance(position);
						closestP = position;
					}
				}
			}
			if (unit->getLastCommand().getTargetPosition().getDistance(closestP) > 5 || unit->getLastCommandFrame() + 100 < Broodwar->getFrameCount())
			{
				unit->move(Position(closestP.x + rand() % 3 + (-1), closestP.y + rand() % 3 + (-1)));
				return;
			}
		}
		else
		{
			// Else just defend at nearest chokepoint (starting of game without scout information)
			unit->move((getNearestChokepoint(unit->getPosition()))->getCenter());
			return;
		}
	}

	// Check if we should attack
	if (globalStrategy == 1 && TerrainTracker::Instance().getEnemyBasePositions().size() > 0)
	{
		if (target && target->exists() && unit->getDistance(target) < 512)
		{
			unitMicroTarget(unit, target);
			return;
		}
		unit->attack(TerrainTracker::Instance().getEnemyBasePositions().front());
		return;
	}
}

void CommandTrackerClass::updateLocalStrategy(Unit unit, Unit target)
{
	// Variables for calculating local strengths	
	double enemyLocalStrength = 0.0, allyLocalStrength = 0.0, thisUnit = 0.0;
	Position targetPosition = UnitTracker::Instance().getEnUnits()[target].getPosition();
	int radius = min(800, 384 + UnitTracker::Instance().getSupply() * 4);

	int aLarge = UnitTracker::Instance().getMySizes()[UnitSizeTypes::Large];
	int aMedium = UnitTracker::Instance().getMySizes()[UnitSizeTypes::Medium];
	int aSmall = UnitTracker::Instance().getMySizes()[UnitSizeTypes::Small];

	int eLarge = UnitTracker::Instance().getEnSizes()[UnitSizeTypes::Large];
	int eMedium = UnitTracker::Instance().getEnSizes()[UnitSizeTypes::Medium];
	int eSmall = UnitTracker::Instance().getEnSizes()[UnitSizeTypes::Small];

	// Reset local
	UnitTracker::Instance().getMyUnits()[unit].setLocal(0);

	if (unit->getDistance(targetPosition) > 512)
	{
		UnitTracker::Instance().getMyUnits()[unit].setStrategy(3);
		return;
	}

	// Check every enemy unit being in range of the target
	for (auto &u : UnitTracker::Instance().getEnUnits())
	{
		// Reset unit strength
		thisUnit = 0.0;

		// Ignore workers, keep buildings (reinforcements and static defenses)
		if (u.second.getUnitType().isWorker() || (u.first && u.first->exists() && u.first->isStasised()))
		{
			continue;
		}

		// If a unit is within range of the target, add to local strength
		if (u.second.getPosition().getDistance(targetPosition) < radius)
		{
			if (aLarge > 0 || aMedium > 0 || aSmall > 0)
			{
				// If unit is cloaked or burrowed and not detected, drastically increase strength
				if ((u.first->isCloaked() || u.first->isBurrowed()) && !u.first->isDetected())
				{
					thisUnit = 20 * u.second.getStrength();
				}
				else if (u.first->getType().groundWeapon().damageType() == DamageTypes::Explosive)
				{
					thisUnit = u.second.getStrength() * ((aLarge*1.0) + (aMedium*0.75) + (aSmall*0.5)) / (aLarge + aMedium + aSmall);
				}
				else if (u.first->getType().groundWeapon().damageType() == DamageTypes::Concussive)
				{
					thisUnit = u.second.getStrength() * ((aLarge*1.0) + (aMedium*0.75) + (aSmall*0.5)) / (aLarge + aMedium + aSmall);
				}
				else
				{
					thisUnit = u.second.getStrength();
				}
			}
			else
			{
				thisUnit = u.second.getStrength();
			}
			// If enemy hasn't died, add to enemy. Otherwise, partially add to ally local
			if (u.second.getDeadFrame() == 0)
			{
				enemyLocalStrength += thisUnit;
			}
			else
			{
				allyLocalStrength += u.second.getMaxStrength() * 1.0 / (1.0 + 0.01*(double(Broodwar->getFrameCount()) - double(u.second.getDeadFrame())));
			}
		}
	}

	// Check every ally being in range of the target
	for (auto &u : UnitTracker::Instance().getMyUnits())
	{
		// Reset unit strength
		thisUnit = 0.0;

		// Ignore workers and buildings
		if (u.second.getUnitType().isWorker() || u.second.getUnitType().isBuilding())
		{
			continue;
		}

		// If a unit is within the range of the ally unit, add to local strength
		if (u.second.getPosition().getDistance(unit->getPosition()) < radius)
		{
			if (eLarge > 0 || eMedium > 0 || eSmall > 0)
			{
				// If shuttle, add units inside
				if (u.second.getUnitType() == UnitTypes::Protoss_Shuttle && u.first->getLoadedUnits().size() > 0)
				{
					// Assume reaver for damage type calculations
					for (Unit uL : u.first->getLoadedUnits())
					{
						thisUnit = UnitTracker::Instance().getMyUnits()[uL].getStrength();
					}
				}
				else
				{
					// Damage type calculations
					if (u.second.getUnitType().groundWeapon().damageType() == DamageTypes::Explosive)
					{
						thisUnit = u.second.getStrength() * ((eLarge*1.0) + (eMedium*0.75) + (eSmall*0.5)) / (eLarge + eMedium + eSmall);
					}
					else if (u.second.getUnitType().groundWeapon().damageType() == DamageTypes::Concussive)
					{
						thisUnit = u.second.getStrength() * ((eLarge*1.0) + (eMedium*0.75) + (eSmall*0.5)) / (eLarge + eMedium + eSmall);
					}
					else
					{
						thisUnit = u.second.getStrength();
					}
				}
			}
			else
			{
				thisUnit = u.second.getStrength();
			}

			// If ally hasn't died, add to ally. Otherwise, partially add to enemy local
			if (u.second.getDeadFrame() == 0)
			{
				allyLocalStrength += thisUnit;
			}
			else
			{
				enemyLocalStrength += u.second.getMaxStrength() * 1.0 / (1.0 + 0.01*(double(Broodwar->getFrameCount()) - double(u.second.getDeadFrame())));
			}
		}
	}

	// Store the difference of strengths 
	UnitTracker::Instance().getMyUnits()[unit].setLocal(allyLocalStrength - enemyLocalStrength);

	// If we are in ally territory and have a target, force to fight	
	if (target && target->exists())
	{
		if (TerrainTracker::Instance().getAllyTerritory().find(getRegion(target->getPosition())) != TerrainTracker::Instance().getAllyTerritory().end())
		{

			UnitTracker::Instance().getMyUnits()[unit].setStrategy(1);
			return;
		}
	}

	// Force Zealots to stay on Tanks
	if (unit->getType() == UnitTypes::Protoss_Zealot && target->exists() && (UnitTracker::Instance().getEnUnits()[target].getUnitType() == UnitTypes::Terran_Siege_Tank_Siege_Mode || UnitTracker::Instance().getEnUnits()[target].getUnitType() == UnitTypes::Terran_Siege_Tank_Tank_Mode) && unit->getDistance(targetPosition) < 128)
	{
		UnitTracker::Instance().getMyUnits()[unit].setStrategy(1);
		return;
	}

	// If unit is in range of a target and not currently threatened, attack instead of running
	if (unit->getDistance(targetPosition) <= UnitTracker::Instance().getMyUnits()[unit].getRange() && (GridTracker::Instance().getEGroundGrid(unit->getTilePosition().x, unit->getTilePosition().y) == 0 || unit->getType() == UnitTypes::Protoss_Reaver))
	{
		UnitTracker::Instance().getMyUnits()[unit].setStrategy(1);
		return;
	}

	// If last command was engage
	if (UnitTracker::Instance().getMyUnits()[unit].getStrategy() == 1)
	{
		// Latch based system for at least 80% disadvantage to disengage
		if (allyLocalStrength < enemyLocalStrength*0.8)
		{
			UnitTracker::Instance().getMyUnits()[unit].setStrategy(0);
			return;
		}
		UnitTracker::Instance().getMyUnits()[unit].setStrategy(1);		
		return;
	}
	// If last command was disengage/no command
	else
	{
		// Latch based system for at least 120% advantage to engage
		if (allyLocalStrength >= enemyLocalStrength*1.2)
		{			
			UnitTracker::Instance().getMyUnits()[unit].setStrategy(1);
			return;
		}
		// Otherwise return 3 or 0, whichever was previous
		UnitTracker::Instance().getMyUnits()[unit].setStrategy(0);
		return;
	}
	// Disregard local if no target, no recent local calculation and not within ally region
	UnitTracker::Instance().getMyUnits()[unit].setStrategy(3);
	return;
}

void CommandTrackerClass::updateGlobalStrategy(Unit unit, Unit target)
{
	if (StrategyTracker::Instance().isFastExpand())
	{
		globalStrategy = 0;
		return;
	}
	if (StrategyTracker::Instance().globalAlly() > StrategyTracker::Instance().globalEnemy())
	{
		// If Zerg, wait for a larger army before moving out
		if (Broodwar->enemy()->getRace() == Races::Zerg && Broodwar->self()->completedUnitCount(UnitTypes::Protoss_Dragoon) == 0)
		{
			globalStrategy = 0;
			return;
		}
		globalStrategy = 1;
		return;
	}
	else
	{
		// If not Zerg, contain
		if (Broodwar->enemy()->getRace() != Races::Zerg)
		{
			globalStrategy = 1;
			return;
		}
		globalStrategy = 0;
		return;
	}
	globalStrategy = 1;
	return;
}

void CommandTrackerClass::unitMicroTarget(Unit unit, Unit target)
{
	// Variables
	bool kite = false;	
	int range = UnitTracker::Instance().getMyUnits()[unit].getRange();
	UnitTracker::Instance().getMyUnits()[unit].setTargetPosition(Positions::None);
	int offset = 0;	

	// Stop offset required for units with animations
	if (unit->getType() == UnitTypes::Protoss_Dragoon)
	{
		offset = 9;
	}
	if (unit->getType() == UnitTypes::Protoss_Reaver)
	{
		offset = 1;
	}

	// If unit receieved an attack command on the target already, don't give another order - TODO: Test if it could be removed maybe to prevent goon stop bug
	if (unit->getLastCommand().getType() == UnitCommandTypes::Attack_Unit && unit->getTarget() == target)
	{
		return;
	}

	// If kiting unnecessary, disable
	if (target->getType().isBuilding() || unit->getType() == UnitTypes::Protoss_Corsair)
	{
		kite = false;
	}

	else if (unit->getType() == UnitTypes::Protoss_Reaver && UnitTracker::Instance().getEnUnits()[target].getRange() < range)
	{
		kite = true;
	}

	// If kiting is a good idea, enable
	else if (target->getType() == UnitTypes::Terran_Vulture_Spider_Mine || (range > 32 && unit->isUnderAttack()) || (UnitTracker::Instance().getEnUnits()[target].getRange() <= range && (unit->getDistance(target) < range - UnitTracker::Instance().getEnUnits()[target].getRange() && UnitTracker::Instance().getEnUnits()[target].getRange() > 0 && range > 32 || unit->getHitPoints() < 40)))
	{
		kite = true;
	}

	// If kite is true and weapon on cooldown, move
	if (kite && Broodwar->getFrameCount() - UnitTracker::Instance().getMyUnits()[unit].getLastCommandFrame() > offset - Broodwar->getLatencyFrames() && unit->getGroundWeaponCooldown() > 0)
	{
		Position correctedFleePosition = unitFlee(unit, target);
		// Want Corsairs to move closer always if possible
		if (unit->getType() == UnitTypes::Protoss_Corsair)
		{
			unit->move(target->getPosition());
			UnitTracker::Instance().getMyUnits()[unit].setTargetPosition(target->getPosition());
		}
		else if (correctedFleePosition != BWAPI::Positions::None && (unit->getLastCommand().getType() != UnitCommandTypes::Move || unit->getLastCommand().getTargetPosition().getDistance(correctedFleePosition) > 5))
		{
			unit->move(Position(correctedFleePosition.x + rand() % 3 + (-1), correctedFleePosition.y + rand() % 3 + (-1)));
			UnitTracker::Instance().getMyUnits()[unit].setTargetPosition(correctedFleePosition);
		}
	}
	// Else, regardless of if kite is true or not, attack if weapon is off cooldown
	else if (unit->getGroundWeaponCooldown() <= 0)
	{
		unit->attack(target);	
		UnitTracker::Instance().getMyUnits()[unit].setTargetPosition(target->getPosition());
	}
	return;
}

void CommandTrackerClass::unitExploreArea(Unit unit)
{
	// Given a region, explore a random portion of it based on random metrics like:
	// Distance to enemy
	// Distance to home
	// Last explored
	// Unit deaths
	// Untouched resources
}

Position CommandTrackerClass::unitFlee(Unit unit, Unit currentTarget)
{
	// If either the unit or current target are invalid, return
	if (!unit || !currentTarget)
	{
		return Positions::None;
	}

	// Unit Flee Variables
	double slopeDegree;
	int x, y;
	Position currentTargetPosition = UnitTracker::Instance().getEnUnits()[currentTarget].getPosition();
	Position currentUnitPosition = unit->getPosition();
	TilePosition currentUnitTilePosition = unit->getTilePosition();

	// Divide by zero check, if zero then we are fleeing horizontally, no problem if fleeing vertically.
	if ((currentUnitPosition.x - currentTargetPosition.x) == 0)
	{
		slopeDegree = 1.571;
	}
	else
	{
		slopeDegree = atan((currentUnitPosition.y - currentTargetPosition.y) / (currentUnitPosition.x - currentTargetPosition.x));
	}

	// Need to make sure we are fleeing at the same angle the units are attacking each other at
	if (currentUnitPosition.x > currentTargetPosition.x)
	{
		x = (int)(currentUnitTilePosition.x + (5 * cos(slopeDegree)));
	}
	else
	{
		x = (int)(currentUnitTilePosition.x - (5 * cos(slopeDegree)));
	}
	if (currentUnitPosition.y > currentTargetPosition.y)
	{
		y = (int)(currentUnitTilePosition.y + abs(5 * sin(slopeDegree)));
	}
	else
	{
		y = (int)(currentUnitTilePosition.y - abs(5 * sin(slopeDegree)));
	}

	Position initialPosition = Position(TilePosition(x, y));
	Position finalPosition = initialPosition;
	int bestPos = 0;
	for (int i = initialPosition.x - 3; i <= initialPosition.x + 3; i++)
	{
		for (int i = initialPosition.y - 3; i <= initialPosition.y + 3; i++)
		{
			if (GridTracker::Instance().getEGroundGrid(x, y) < 1 && GridTracker::Instance().getMobilityGrid(x, y) > 0)
			{
				bestPos = GridTracker::Instance().getMobilityGrid(x, y);
				finalPosition = Position(x * 32, y * 32);
			}
		}
	}
	//// Spiral variables
	//int length = 1;
	//int j = 0;
	//bool first = true;
	//bool okay = false;
	//int dx = 0;
	//int dy = 1;
	//// Searches in a spiral around the specified tile position
	//while (length < 2000)
	//{
	//	//If threat is low, move there
	//	if (x >= 0 && x < BWAPI::Broodwar->mapWidth() && y >= 0 && y < BWAPI::Broodwar->mapHeight())
	//	{
	//		Position newPosition = Position(TilePosition(x, y));
	//		if (GridTracker::Instance().getEGroundGrid(x, y) < 1 && GridTracker::Instance().getMobilityGrid(x, y) > 0 && (newPosition.getDistance(getNearestChokepoint(currentUnitPosition)->getCenter()) < 128 || (getRegion(currentUnitPosition)) == getRegion(newPosition)) && Broodwar->getUnitsOnTile(TilePosition(x, y)).size() < 2)
	//		{
	//			return newPosition;
	//		}
	//	}
	//	//Otherwise, move to another position
	//	x = x + dx;
	//	y = y + dy;
	//	//Count how many steps we take in this direction
	//	j++;
	//	if (j == length) //if we've reached the end, its time to turn
	//	{
	//		//reset step counter
	//		j = 0;

	//		//Increment step counter
	//		if (!first)
	//			length++;

	//		//First=true for every other turn so we spiral out at the right rate
	//		first = !first;

	//		//Turn counter clockwise 90 degrees:
	//		if (dx == 0)
	//		{
	//			dx = dy;
	//			dy = 0;
	//		}
	//		else
	//		{
	//			dy = -dx;
	//			dx = 0;
	//		}
	//	}
	//}
	return finalPosition;
}