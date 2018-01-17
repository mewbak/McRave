#include "McRave.h"

namespace McRave
{
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
			if ((unit.getType() == UnitTypes::Protoss_Scarab || unit.getType() == UnitTypes::Terran_Vulture_Spider_Mine) && unit.hasTarget() && unit.getTarget().unit()->exists()) moveGroup.insert(unit.getTarget().unit());
		}

		for (auto &u : Units().getAllyUnits())
		{
			UnitInfo &unit = u.second;
			if (!unit.unit() || !unit.unit()->exists()) continue; // Prevent crashes		
			if (unit.getType() == UnitTypes::Protoss_Observer || unit.getType() == UnitTypes::Protoss_Arbiter || unit.getType() == UnitTypes::Protoss_Shuttle) continue; // Support units and transports have their own commands
			if (unit.getLastCommandFrame() >= Broodwar->getFrameCount()) continue; // If the unit received a command already during this frame		
			if (unit.unit()->isLockedDown() || unit.unit()->isMaelstrommed() || unit.unit()->isStasised() || !unit.unit()->isCompleted()) continue; // If the unit is locked down, maelstrommed, stassised, or not completed

			// If the unit is ready to perform an action after an attack (certain units have minimum frames after an attack before they can receive a new command)
			if (Broodwar->getFrameCount() - unit.getLastAttackFrame() > unit.getMinStopFrame() - Broodwar->getLatencyFrames())
			{
				// If we are the target of a mine or scarab, move forward
				if (moveGroup.find(unit.unit()) != moveGroup.end())
				{
					if (unit.unit()->getGroundWeaponCooldown() > 0) approach(unit);
					else if (unit.hasTarget() && unit.hasTarget() && unit.getTarget().unit()->exists()) attack(unit);
				}
				else if (Grids().getPsiStormGrid(unit.getWalkPosition()) > 0 || Grids().getEMPGrid(unit.getWalkPosition()) > 0 || Grids().getESplashGrid(unit.getWalkPosition()) > 0) flee(unit); // If under a storm, dark swarm or EMP

				// If globally behind
				else if ((Units().getGlobalGroundStrategy() == 0 && !unit.getType().isFlyer()) || (Units().getGlobalAirStrategy() == 0 && unit.getType().isFlyer()))
				{
					if (unit.getStrategy() == 0) flee(unit); // If locally behind, flee				
					else if (unit.getStrategy() == 1 && unit.hasTarget() && unit.getTarget().unit() && unit.getTarget().unit()->exists()) engage(unit); // If locally ahead, attack
					else defend(unit);
				}

				// If globally ahead
				else if ((Units().getGlobalGroundStrategy() == 1 && !unit.getType().isFlyer()) || (Units().getGlobalAirStrategy() == 1 && unit.getType().isFlyer()))
				{
					if (unit.getStrategy() == 0) flee(unit); // If locally behind, flee				
					else if (unit.getStrategy() == 1 && unit.hasTarget() && unit.getTarget().unit() && unit.getTarget().unit()->exists()) engage(unit); // If locally ahead, attack				
					else if (unit.getStrategy() == 2) defend(unit); // If within ally territory, defend				
					else move(unit); // Else move unit
				}
			}
		}
		return;
	}

	void CommandTrackerClass::engage(UnitInfo& unit)
	{
		bool moveAway = false;
		bool moveTo = false;

		// Specific High Templar behavior
		if (unit.getType() == UnitTypes::Protoss_High_Templar)
		{
			if (unit.hasTarget() && unit.getTarget().unit()->exists() && unit.unit()->getEnergy() >= 75 && Grids().getEGroundCluster(unit.getTarget().getWalkPosition()) + Grids().getEAirCluster(unit.getTarget().getWalkPosition()) > 3.0)
			{
				unit.unit()->useTech(TechTypes::Psionic_Storm, unit.getTarget().unit());
				Grids().updatePsiStorm(unit.getTarget().getWalkPosition());
				return;
			}
			else
			{
				flee(unit);
				return;
			}
		}

		// Specific Marine and Firebat behavior	
		if ((unit.getType() == UnitTypes::Terran_Marine || unit.getType() == UnitTypes::Terran_Firebat) && !unit.unit()->isStimmed() && unit.getTarget().getPosition().isValid() && unit.unit()->getDistance(unit.getTarget().getPosition()) <= unit.getGroundRange())
		{
			unit.unit()->useTech(TechTypes::Stim_Packs);
		}

		// Specific Tank behavior
		if (unit.getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode)
		{
			if (unit.unit()->getDistance(unit.getTarget().getPosition()) <= 400 && unit.unit()->getDistance(unit.getTarget().getPosition()) > 128)
				unit.unit()->siege();
		}
		if (unit.getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			if (unit.unit()->getDistance(unit.getTarget().getPosition()) > 400 || unit.unit()->getDistance(unit.getTarget().getPosition()) < 128)
				unit.unit()->unsiege();
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

		// If enemy is invis, run from it
		if ((unit.getTarget().unit()->isBurrowed() || unit.getTarget().unit()->isCloaked()) && !unit.getTarget().unit()->isDetected() && unit.getTarget().getPosition().getDistance(unit.getPosition()) < (unit.getTarget().getGroundRange() + unit.getTarget().getSpeed()))
		{
			flee(unit);
		}

		// TODO
		if (shouldAttack(unit)) attack(unit);
		else if (shouldApproach(unit)) approach(unit);
		else if (shouldKite(unit)) flee(unit);
	}

	void CommandTrackerClass::move(UnitInfo& unit)
	{
		// If it's a tank, make sure we're unsieged before moving - TODO: Check that target has velocity and > 512 or no velocity and < tank range
		if (unit.hasTarget() && unit.getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			if (unit.unit()->getDistance(unit.getTarget().getPosition()) > 512 || unit.unit()->getDistance(unit.getTarget().getPosition()) < 64)
				unit.unit()->unsiege();
		}

		// If unit has a transport, move to it or load into it
		else if (unit.getTransport() && unit.getTransport()->exists())
		{
			if (!isLastCommand(unit, UnitCommandTypes::Right_Click_Unit, unit.getTransport()->getPosition()))
				unit.unit()->rightClick(unit.getTransport());
		}

		// If target doesn't exist, move towards it
		else if (unit.hasTarget() && unit.getTarget().getPosition().isValid() && Grids().getMobilityGrid(WalkPosition(unit.getEngagePosition())) > 0)
		{
			if (!isLastCommand(unit, UnitCommandTypes::Move, unit.getTarget().getPosition()))
				unit.unit()->move(unit.getTarget().getPosition());
		}

		else if (Terrain().getAttackPosition().isValid() && !unit.getType().isFlyer())
		{
			if (!isLastCommand(unit, UnitCommandTypes::Move, Terrain().getAttackPosition()))
				unit.unit()->move(Terrain().getAttackPosition());
		}

		// If no target and no enemy bases, move to a random base
		else if (unit.unit()->isIdle())
		{
			int random = rand() % (Terrain().getAllBases().size() - 1);
			int i = 0;
			for (auto &base : Terrain().getAllBases())
			{
				if (i == random)
				{
					unit.unit()->move(base->Center());
					return;
				}
				else i++;
			}
		}
	}

	void CommandTrackerClass::defend(UnitInfo& unit)
	{
		if (unit.getType().isFlyer())
		{
			unit.unit()->move(Position(mapBWEB.getNatural()));
			return;
		}

		// Early on, defend mineral line
		if (!Strategy().isHoldChoke())
		{
			if (unit.getType() == UnitTypes::Protoss_Dragoon && Terrain().getBackMineralHoldPosition().isValid())
			{
				if (unit.unit()->getLastCommand().getTargetPosition() != Terrain().getBackMineralHoldPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move)
					unit.unit()->move(Terrain().getBackMineralHoldPosition());
			}
			else
			{
				if (unit.unit()->getLastCommand().getTargetPosition() != Terrain().getMineralHoldPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move)
					unit.unit()->move(Terrain().getMineralHoldPosition());
			}
		}
		else
		{
			// Defend chokepoint with concave
			int min = max(64, int(unit.getGroundRange()));
			int max = int(unit.getGroundRange()) + 256;
			double closestD = DBL_MAX;
			WalkPosition start = unit.getWalkPosition();
			WalkPosition bestPosition = start;

			// Find closest chokepoint to defend
			WalkPosition choke;
			if (BuildOrder().getBuildingDesired()[UnitTypes::Protoss_Nexus] >= 2 || BuildOrder().getBuildingDesired()[UnitTypes::Terran_Command_Center] >= 2 || Strategy().isAllyFastExpand())
				choke = WalkPosition(mapBWEB.getSecondChoke());
			else choke = WalkPosition(mapBWEB.getFirstChoke());

			// Find suitable position to hold at chokepoint
			if (Terrain().isInAllyTerritory(unit.getTilePosition())) closestD = unit.getPosition().getDistance(Position(choke));
			for (int x = choke.x - 35; x <= choke.x + 35; x++)
			{
				for (int y = choke.y - 35; y <= choke.y + 35; y++)
				{
					if (!WalkPosition(x, y).isValid() || !Util().isMobile(start, WalkPosition(x, y), unit.getType())) continue;

					if (Terrain().isInAllyTerritory(TilePosition(WalkPosition(x, y))) && Position(WalkPosition(x, y)).getDistance(Position(choke)) > min && Position(WalkPosition(x, y)).getDistance(Position(choke)) < max && Position(WalkPosition(x, y)).getDistance(Position(choke)) < closestD)
					{
						bestPosition = WalkPosition(x, y);
						closestD = Position(WalkPosition(x, y)).getDistance(Position(choke));
					}
				}
			}

			if (bestPosition.isValid() && bestPosition != start)
			{
				if (unit.unit()->getLastCommand().getTargetPosition() != Position(bestPosition) || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move || unit.getPosition().getDistance(Position(bestPosition)) > 32)
				{
					unit.unit()->move(Position(bestPosition));
				}
				unit.setDestination(Position(bestPosition));
				Grids().updateAllyMovement(unit.unit(), bestPosition);
			}
		}
	}

	void CommandTrackerClass::flee(UnitInfo& unit)
	{
		double widths = unit.getTarget().getType().tileWidth() * 16.0 + unit.getType().tileWidth() * 16.0;
		double allyRange = widths + (unit.getTarget().getType().isFlyer() ? unit.getAirRange() : unit.getGroundRange());
		double enemyRange = widths + (unit.getType().isFlyer() ? unit.getTarget().getAirRange() : unit.getTarget().getGroundRange());

		if (unit.getType().isWorker() && unit.getPosition().getDistance(Terrain().getMineralHoldPosition()) < 256)
		{
			unit.unit()->gather(unit.unit()->getClosestUnit(Filter::IsMineralField, 128));
			return;
		}

		if (unit.getTransport() && unit.getTransport()->exists()) return;

		// If it's a tank, make sure we're unsieged before moving -  TODO: Check that target has velocity and > 512 or no velocity and < tank range
		if (unit.getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			if (unit.unit()->getDistance(unit.getTarget().getPosition()) > 512 || unit.unit()->getDistance(unit.getTarget().getPosition()) < 64)
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
				UnitInfo &templar = Units().getUnitInfo(t);
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

				if (Terrain().isInAllyTerritory(unit.getTilePosition())) distance = 1.0 / (32.0 + Position(WalkPosition(x, y)).getDistance(unit.getTarget().getPosition()));
				else distance = unit.getType().isFlyer() ? Position(WalkPosition(x, y)).getDistance(Terrain().getPlayerStartingPosition()) : double(Grids().getDistanceHome(x, y)); // Distance value	

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
				unit.unit()->move(Position(bestPosition));
		}
	}

	void CommandTrackerClass::attack(UnitInfo& unit)
	{
		if (unit.unit()->getLastCommand().getType() != UnitCommandTypes::Attack_Unit || unit.unit()->getLastCommand().getTarget() != unit.getTarget().unit())
			unit.unit()->attack(unit.getTarget().unit());
	}

	void CommandTrackerClass::approach(UnitInfo& unit)
	{
		// TODO, use linear interpolation to approach closer, so that units can approach a cliff to snipe units on higher terrain (carriers, tanks)
		if (unit.getTarget().getPosition().isValid() && (unit.unit()->getLastCommand().getTargetPosition() != unit.getTarget().getPosition() || unit.unit()->getLastCommand().getType() != UnitCommandTypes::Move))
			unit.unit()->move(unit.getTarget().getPosition());
	}

	bool CommandTrackerClass::shouldAttack(UnitInfo& unit)
	{
		double widths = unit.getTarget().getType().tileWidth() * 16.0 + unit.getType().tileWidth() * 16.0;
		double enemyRange = widths + (unit.getType().isFlyer() ? unit.getTarget().getAirRange() : unit.getTarget().getGroundRange());
		double allyRange = widths + (unit.getTarget().getType().isFlyer() ? unit.getAirRange() : unit.getGroundRange());

		if (!unit.getTarget().getType().isFlyer() && unit.unit()->getGroundWeaponCooldown() <= 0) return true;
		else if (unit.getTarget().getType().isFlyer() && unit.unit()->getAirWeaponCooldown() <= 0) return true;
		else if (unit.getType() == UnitTypes::Terran_Medic) return true;
		return false;
	}

	bool CommandTrackerClass::shouldKite(UnitInfo& unit)
	{
		double widths = unit.getTarget().getType().tileWidth() * 16.0 + unit.getType().tileWidth() * 16.0;
		double enemyRange = widths + (unit.getType().isFlyer() ? unit.getTarget().getAirRange() : unit.getTarget().getGroundRange());
		double allyRange = widths + (unit.getTarget().getType().isFlyer() ? unit.getAirRange() : unit.getGroundRange());

		if (Terrain().isInAllyTerritory(unit.getTilePosition()) && allyRange <= 32) return false;

		if (unit.getTarget().getType().isBuilding()) return false; // Don't kite buildings
		else if (unit.getType() == UnitTypes::Protoss_Reaver && unit.getTarget().getGroundRange() < unit.getGroundRange()) return true; // Reavers should always kite away from their target if it has lower range
		else if (allyRange > 32.0 && unit.unit()->isUnderAttack() && allyRange >= enemyRange) return true;
		else if (enemyRange <= allyRange && (unit.unit()->getDistance(unit.getTarget().getPosition()) <= allyRange - enemyRange) || unit.unit()->getHitPoints() < 40) return true;
		return false;
	}

	bool CommandTrackerClass::shouldApproach(UnitInfo& unit)
	{
		double widths = unit.getTarget().getType().tileWidth() * 16.0 + unit.getType().tileWidth() * 16.0;
		double enemyRange = widths + (unit.getType().isFlyer() ? unit.getTarget().getAirRange() : unit.getTarget().getGroundRange());
		double allyRange = widths + (unit.getTarget().getType().isFlyer() ? unit.getAirRange() : unit.getGroundRange());

		if (unit.getGroundRange() > 32 && unit.getGroundRange() < unit.getTarget().getGroundRange() && !unit.getTarget().getType().isBuilding() && unit.getSpeed() > unit.getTarget().getSpeed() && Grids().getMobilityGrid(WalkPosition(unit.getEngagePosition())) > 0) return true;
		else if (unit.getType().isFlyer() && unit.getTarget().getType() != UnitTypes::Zerg_Scourge) return true;
		return false;
	}

	bool CommandTrackerClass::isLastCommand(UnitInfo& unit, UnitCommandType thisCommand, Position thisPosition)
	{
		if (unit.getLastCommand().getType() == thisCommand && unit.getLastCommand().getTargetPosition() == thisPosition) return true;
		return false;
	}
}