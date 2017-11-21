#include "McRave.h"

void CommandTrackerClass::update()
{
	Display().startClock();
	updateAlliedUnits();
	Display().performanceTest(__FUNCTION__);
	return;
}

void CommandTrackerClass::updateAlliedUnits()
{
	// Add units that are targets of large splash threats
	set <Unit> moveGroup;
	for (auto &u : Units().getEnemyUnits())
	{
		UnitInfo &unit = u.second;
		if ((unit.getType() == UnitTypes::Protoss_Scarab || unit.getType() == UnitTypes::Terran_Vulture_Spider_Mine) && unit.getTarget() && unit.getTarget()->exists()) moveGroup.insert(unit.getTarget());
	}

	for (auto &u : Units().getAllyUnits())
	{
		UnitInfo &unit = u.second;
		if (!unit.unit() || !unit.unit()->exists()) continue; // Prevent crashes		
		if (unit.getType() == UnitTypes::Protoss_Observer || unit.getType() == UnitTypes::Protoss_Arbiter || unit.getType() == UnitTypes::Protoss_Shuttle) continue; // Support units and transports have their own commands
		if (unit.getLastCommandFrame() >= Broodwar->getFrameCount()) continue; // If the unit received a command already during this frame		
		if (unit.unit()->isLockedDown() || unit.unit()->isMaelstrommed() || unit.unit()->isStasised() || !unit.unit()->isCompleted()) continue; // If the unit is locked down, maelstrommed, stassised, or not completed

		// Remove wall if needed - testing
		if (Units().getSupply() > 200 && unit.unit()->isStuck())
		{
			Unit wall = Broodwar->getClosestUnit(Position(Terrain().getLargeWall()), Filter::IsAlly && Filter::GetType == UnitTypes::Protoss_Gateway, 32);
			unit.unit()->attack(wall);
			continue;
		}

		// If the unit is ready to perform an action after an attack (certain units have minimum frames after an attack before they can receive a new command)
		if (Broodwar->getFrameCount() - unit.getLastAttackFrame() > unit.getMinStopFrame() - Broodwar->getLatencyFrames())
		{
			// If we are the target of a mine or scarab, move forward
			if (moveGroup.find(unit.unit()) != moveGroup.end())
			{
				if (unit.unit()->getGroundWeaponCooldown() > 0) approach(unit);
				else attack(unit);
			}
			else if (Grids().getPsiStormGrid(unit.getWalkPosition()) > 0 || Grids().getEMPGrid(unit.getWalkPosition()) > 0 || Grids().getESplashGrid(unit.getWalkPosition()) > 0) flee(unit); // If under a storm, dark swarm or EMP

			// If globally behind
			else if ((Units().getGlobalGroundStrategy() == 0 && !unit.getType().isFlyer()) || (Units().getGlobalAirStrategy() == 0 && unit.getType().isFlyer()))
			{
				if (unit.getStrategy() == 0) flee(unit); // If locally behind, flee				
				else if (unit.getStrategy() == 1 && unit.getTarget()->exists()) attack(unit); // If locally ahead, attack
				else defend(unit);
			}

			// If globally ahead
			else if ((Units().getGlobalGroundStrategy() == 1 && !unit.getType().isFlyer()) || (Units().getGlobalAirStrategy() == 1 && unit.getType().isFlyer()))
			{
				if (unit.getStrategy() == 0) flee(unit); // If locally behind, flee				
				else if (unit.getStrategy() == 1 && unit.getTarget()->exists()) attack(unit); // If locally ahead, attack				
				else if (unit.getStrategy() == 2) defend(unit); // If within ally territory, defend				
				else move(unit); // Else move unit
			}
		}
	}
	return;
}

void CommandTrackerClass::attack(UnitInfo& unit)
{
	// Specific Medic behavior
	if (unit.getType() == UnitTypes::Terran_Medic)
	{
		UnitInfo &target = Units().getAllyUnit(unit.getTarget());
		if (target.getPercentHealth() < 1.0)
		{
			if (!isLastCommand(unit, UnitCommandTypes::Move, unit.getTargetPosition())) unit.unit()->useTech(TechTypes::Healing, unit.getTarget());
		}
		else
		{
			if (!isLastCommand(unit, UnitCommandTypes::Move, unit.getTargetPosition())) unit.unit()->move(unit.getTargetPosition());
		}
		return;
	}
	else
	{
		// TEMP -- Set to false initially
		bool moveAway = false;
		bool moveTo = false;
		UnitInfo &target = Units().getEnemyUnit(unit.getTarget());

		// Specific High Templar behavior
		if (unit.getType() == UnitTypes::Protoss_High_Templar)
		{
			if (unit.getTarget() && unit.getTarget()->exists() && unit.unit()->getEnergy() >= 75)
			{
				unit.unit()->useTech(TechTypes::Psionic_Storm, unit.getTarget());
				Grids().updatePsiStorm(unit.getTargetWalkPosition());
				return;
			}
		}

		// Specific Marine and Firebat behavior	
		if ((unit.getType() == UnitTypes::Terran_Marine || unit.getType() == UnitTypes::Terran_Firebat) && !unit.unit()->isStimmed() && unit.getTargetPosition().isValid() && unit.unit()->getDistance(unit.getTargetPosition()) <= unit.getGroundRange())
		{
			unit.unit()->useTech(TechTypes::Stim_Packs);
		}

		// Specific Tank behavior
		if (unit.getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode)
		{
			if (unit.unit()->getDistance(unit.getTargetPosition()) <= 400 && unit.unit()->getDistance(unit.getTargetPosition()) > 128)
			{
				unit.unit()->siege();
			}
		}
		if (unit.getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			if (unit.unit()->getDistance(unit.getTargetPosition()) > 400 || unit.unit()->getDistance(unit.getTargetPosition()) < 128)
			{
				unit.unit()->unsiege();
			}
		}

		// If we can use a Shield Battery
		if (Grids().getBatteryGrid(unit.getTilePosition()) > 0 && ((unit.unit()->getLastCommand().getType() == UnitCommandTypes::Right_Click_Unit && unit.unit()->getShields() < 40) || unit.unit()->getShields() < 10))
		{
			if (unit.unit()->getLastCommand().getType() != UnitCommandTypes::Right_Click_Unit)
			{
				for (auto& b : Buildings().getMyBuildings())
				{
					BuildingInfo &building = b.second;
					if (building.getType() == UnitTypes::Protoss_Shield_Battery && building.getEnergy() >= 10 && unit.unit()->getDistance(building.getPosition()) < 320)
					{
						unit.unit()->rightClick(building.unit());
						continue;
					}
				}
			}
			return;
		}

		// If we can use a bunker
		if (Grids().getBunkerGrid(unit.getTilePosition()) > 0 && unit.getType() == UnitTypes::Terran_Marine)
		{
			Unit bunker = unit.unit()->getClosestUnit(Filter::GetType == UnitTypes::Terran_Bunker && Filter::SpaceRemaining > 0);
			if (bunker)
			{
				unit.unit()->rightClick(bunker);
				return;
			}
		}

		// If kiting unnecessary, disable
		if (unit.getTarget()->getType().isBuilding())
		{
			moveAway = false;
		}

		// If enemy is invis, run from it
		else if ((unit.getTarget()->isBurrowed() || unit.getTarget()->isCloaked()) && !unit.getTarget()->isDetected())
		{
			flee(unit);
		}

		// Reavers should always kite away from their target if it has lower range
		else if (unit.getType() == UnitTypes::Protoss_Reaver && target.getGroundRange() < unit.getGroundRange())
		{
			moveAway = true;
		}

		// If kiting is a good idea, enable
		else if ((unit.getGroundRange() > 32 && unit.unit()->isUnderAttack()) || (target.getGroundRange() <= unit.getGroundRange() && (unit.unit()->getDistance(unit.getTargetPosition()) <= unit.getGroundRange() - target.getGroundRange() && target.getGroundRange() > 0 && unit.getGroundRange() > 32 || unit.unit()->getHitPoints() < 40)))
		{
			moveAway = true;
		}

		// If approaching is necessary
		if (unit.getGroundRange() > 32 && unit.getGroundRange() < target.getGroundRange() && !target.getType().isBuilding() && unit.getSpeed() > target.getSpeed())
		{
			moveTo = true;
		}

		// If kite is true and weapon on cooldown, move
		if ((!target.getType().isFlyer() && unit.unit()->getGroundWeaponCooldown() > 0 || target.getType().isFlyer() && unit.unit()->getAirWeaponCooldown() > 0 || (unit.getTarget()->exists() && (unit.getTarget()->isCloaked() || unit.getTarget()->isBurrowed()) && !unit.getTarget()->isDetected())))
		{
			if (moveTo) approach(unit);
			else if (moveAway) flee(unit);
		}
		else
		{
			if (unit.unit()->getLastCommand().getType() != UnitCommandTypes::Attack_Unit || unit.unit()->getLastCommand().getTarget() != unit.getTarget())
			{
				unit.unit()->attack(unit.getTarget());
			}
		}
	}
	return;
}

void CommandTrackerClass::move(UnitInfo& unit)
{
	// If it's a tank, make sure we're unsieged before moving - TODO: Check that target has velocity and > 512 or no velocity and < tank range
	if (unit.getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
	{
		if (unit.unit()->getDistance(unit.getTargetPosition()) > 512 || unit.unit()->getDistance(unit.getTargetPosition()) < 64)
		{
			unit.unit()->unsiege();
		}
	}

	// If unit has a transport, move to it or load into it
	else if (unit.getTransport() && unit.getTransport()->exists())
	{
		unit.unit()->rightClick(unit.getTransport());
	}

	// If target doesn't exist, move towards it
	else if (unit.getTarget() && unit.getTargetPosition().isValid() && unit.getPosition().getDistance(unit.getTargetPosition()) < 640)
	{
		if (unit.unit()->getLastCommand().getTargetPosition() != unit.getTargetPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move) unit.unit()->move(unit.getTargetPosition());
	}

	else if (Terrain().getAttackPosition().isValid())
	{
		if (unit.unit()->getLastCommand().getTargetPosition() != Terrain().getAttackPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move) unit.unit()->move(Terrain().getAttackPosition());
	}

	// If no target and no enemy bases, move to a random base
	else
	{
		int random = rand() % (Terrain().getAllBaseLocations().size() - 1);
		int i = 0;
		if (unit.unit()->isIdle())
		{
			for (auto &base : Terrain().getAllBaseLocations())
			{
				if (i == random)
				{
					unit.unit()->move(Position(base));
					return;
				}
				else
				{
					i++;
				}
			}
		}
	}
	return;
}

void CommandTrackerClass::defend(UnitInfo& unit)
{
	if (unit.getType().isFlyer() && Units().getGlobalGroundStrategy() == 1)
	{
		unit.unit()->move(Grids().getAllyArmyCenter());
		return;
	}

	// Early on, defend mineral line - TODO: Use ordered bases to check which one to hold
	if (!Strategy().isHoldChoke())
	{
		if (unit.getType() == UnitTypes::Protoss_Dragoon && Terrain().getBackMineralHoldPosition().isValid())
		{
			if (unit.unit()->getLastCommand().getTargetPosition() != Terrain().getBackMineralHoldPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move)
			{
				unit.unit()->move(Terrain().getBackMineralHoldPosition());
			}
			return;
		}
		else
		{
			if (unit.unit()->getLastCommand().getTargetPosition() != Terrain().getMineralHoldPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move)
			{
				unit.unit()->move(Terrain().getMineralHoldPosition());
			}
			return;
		}
	}

	// Defend chokepoint with concave
	int min = max(64, int(unit.getGroundRange()));
	int max = int(unit.getGroundRange()) + 256;
	double closestD = 0.0;
	WalkPosition start = unit.getWalkPosition();
	WalkPosition bestPosition = start;

	// Find closest chokepoint
	WalkPosition choke = WalkPosition(Terrain().getFirstChoke());
	if (BuildOrder().getBuildingDesired()[UnitTypes::Protoss_Nexus] >= 2 || BuildOrder().getBuildingDesired()[UnitTypes::Terran_Command_Center] >= 2 || Strategy().isAllyFastExpand())
	{
		choke = WalkPosition(Terrain().getSecondChoke());
	}

	// Find suitable position to hold at chokepoint
	if (Terrain().isInAllyTerritory(unit.getTilePosition())) closestD = unit.getPosition().getDistance(Position(choke));
	for (int x = choke.x - 35; x <= choke.x + 35; x++)
	{
		for (int y = choke.y - 35; y <= choke.y + 35; y++)
		{
			if (WalkPosition(x, y).isValid() && theMap.GetArea(WalkPosition(x, y)) && Terrain().getAllyTerritory().find(theMap.GetArea(WalkPosition(x, y))->Id()) != Terrain().getAllyTerritory().end() && Position(WalkPosition(x, y)).getDistance(Position(choke)) > min && Position(WalkPosition(x, y)).getDistance(Position(choke)) < max && (Position(WalkPosition(x, y)).getDistance(Position(choke)) < closestD || closestD == 0.0))
			{
				if (Util().isMobile(start, WalkPosition(x, y), unit.getType()))
				{
					bestPosition = WalkPosition(x, y);
					closestD = Position(WalkPosition(x, y)).getDistance(Position(choke));
				}
			}
		}
	}

	if (bestPosition.isValid() && bestPosition != start)
	{
		if (unit.unit()->getLastCommand().getTargetPosition() != Position(bestPosition) || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move)
		{
			unit.unit()->move(Position(bestPosition));
		}
		unit.setDestination(Position(bestPosition));
		Grids().updateAllyMovement(unit.unit(), bestPosition);
	}
	return;
}

void CommandTrackerClass::flee(UnitInfo& unit)
{
	if (unit.getType().isWorker() && Grids().getResourceGrid(unit.getTilePosition()) > 0)
	{
		unit.unit()->gather(unit.unit()->getClosestUnit(Filter::IsMineralField, 128));
		return;
	}

	if (unit.getTransport() && unit.getTransport()->exists()) return;

	// If it's a tank, make sure we're unsieged before moving -  TODO: Check that target has velocity and > 512 or no velocity and < tank range
	if (unit.getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
	{
		if (unit.unit()->getDistance(unit.getTargetPosition()) > 512 || unit.unit()->getDistance(unit.getTargetPosition()) < 64)
		{
			unit.unit()->unsiege();
			return;
		}
	}

	// If it's a High Templar, merge into an Archon if low energy
	if (unit.getType() == UnitTypes::Protoss_High_Templar && unit.unit()->getEnergy() < 75 && Grids().getEGroundThreat(unit.getWalkPosition()) > 0.0)
	{
		for (auto &t : Units().getAllyUnitsFilter(UnitTypes::Protoss_High_Templar))
		{
			UnitInfo &templar = Units().getAllyUnit(t);
			if (templar.unit() && templar.unit()->exists() && templar.unit() != unit.unit() && templar.unit()->getEnergy() < 75 && Grids().getEGroundThreat(templar.getWalkPosition()) > 0.0)
			{
				if (templar.unit()->getLastCommand().getTechType() != TechTypes::Archon_Warp && unit.unit()->getLastCommand().getTechType() != TechTypes::Archon_Warp)
				{
					unit.unit()->useTech(TechTypes::Archon_Warp, templar.unit());
				}
				return;
			}
		}
	}

	WalkPosition start = unit.getWalkPosition();
	WalkPosition bestPosition = WalkPositions::Invalid;
	double best, closest;
	double mobility, distance, threat;
	// Search a 16x16 grid around the unit
	for (int x = start.x - 16; x <= start.x + 16 + (unit.getType().width() / 8.0); x++)
	{
		for (int y = start.y - 16; y <= start.y + 16 + (unit.getType().height() / 8.0); y++)
		{
			if (!WalkPosition(x, y).isValid()) continue;
			if (Grids().getPsiStormGrid(WalkPosition(x, y)) > 0 || Grids().getEMPGrid(WalkPosition(x, y)) > 0 || Grids().getESplashGrid(WalkPosition(x, y)) > 0) continue;

			distance = double(Grids().getDistanceHome(x, y)); // Distance value		
			mobility = unit.getType().isFlyer() ? 1.0 : double(Grids().getMobilityGrid(x, y)); // If unit is a flyer, ignore mobility
			threat = unit.getType().isFlyer() ? max(0.01, Grids().getEAirThreat(x, y)) : max(0.01, Grids().getEGroundThreat(x, y)); // If unit is a flyer, use air threat
			
			if ((!bestPosition.isValid() || distance < closest || (distance <= closest && mobility / threat > best)) && Util().isMobile(start, WalkPosition(x, y), unit.getType()))
			{
				bestPosition = WalkPosition(x, y);
				closest = distance;
				best = mobility / threat;
			}
		}
	}

	// If a valid position was found that isn't the starting position
	if (bestPosition.isValid() && bestPosition != start)
	{
		Grids().updateAllyMovement(unit.unit(), bestPosition);
		unit.setDestination(Position(bestPosition));
		if (unit.unit()->getLastCommand().getTargetPosition() != Position(bestPosition) || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move)
		{
			unit.unit()->move(Position(bestPosition));
		}
	}
	return;
}

void CommandTrackerClass::approach(UnitInfo& unit)
{
	// TODO, use linear interpolation to approach closer, so that units can approach a cliff to snipe units on higher terrain (carriers, tanks)
	if (unit.getTargetPosition().isValid() && (unit.unit()->getLastCommand().getTargetPosition() != unit.getTargetPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move))
	{
		unit.unit()->move(unit.getTargetPosition());
	}
	return;
}

void CommandTrackerClass::exploreArea(UnitInfo& unit)
{
	// Given a region, explore a random portion of it based on random metrics like:
	// Distance to enemy
	// Distance to home
	// Last explored
	// Unit deaths
	// Untouched resources
}

bool CommandTrackerClass::isLastCommand(UnitInfo& unit, UnitCommandType thisCommand, Position thisPosition)
{
	if (unit.getLastCommand().getType() == thisCommand && unit.getLastCommand().getTargetPosition() == thisPosition) return true;
	return false;
}