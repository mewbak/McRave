#include "McRave.h"

void UnitManager::onFrame()
{
	Display().startClock();
	updateUnits();
	Display().performanceTest(__FUNCTION__);
}

void UnitManager::updateUnits()
{
	// Reset calculations and caches
	globalEnemyGroundStrength = 0.0;
	globalEnemyAirStrength = 0.0;
	globalAllyGroundStrength = 0.0;
	globalAllyAirStrength = 0.0;
	immThreat = 0.0;
	proxThreat = 0.0;
	allyDefense = 0.0;
	splashTargets.clear();
	enemyComposition.clear();
	minThreshold = 0.75, maxThreshold = 1.25;

	double currentSim = 0.0;

	// If Zerg
	if (Broodwar->self()->getRace() == Races::Zerg)
		minThreshold = 0.75, maxThreshold = 1.25;

	// If playing PvT
	if (Players().vT())
		minThreshold = 0.5; maxThreshold = 1.5;

	// PvP
	if (Players().vP())
		minThreshold = 0.75, maxThreshold = 1.25;

	if (BuildOrder().isRush())
		minThreshold = 0.0, maxThreshold = 0.75;

	// Update Enemy Units
	for (auto &u : enemyUnits) {
		UnitInfo &unit = u.second;
		if (!unit.unit())
			continue;

		if (unit.unit()->exists() && unit.unit()->isFlying() && unit.getType().isBuilding() && unit.getLastTile().isValid()) {

			for (int x = unit.getLastTile().x; x < unit.getLastTile().x + unit.getType().tileWidth(); x++) {
				for (int y = unit.getLastTile().y; y < unit.getLastTile().y + unit.getType().tileHeight(); y++) {
					TilePosition t(x, y);
					if (!t.isValid())
						continue;

					mapBWEB.getUsedTiles().erase(t);
					mapBWEB.usedGrid[x][y] = 0;
				}
			}
			continue;
		}

		// If unit is visible, update it
		if (unit.unit()->exists()) {
			unit.updateUnit();
			if (unit.hasTarget() && (unit.getType() == UnitTypes::Terran_Vulture_Spider_Mine || unit.getType() == UnitTypes::Protoss_Scarab))
				splashTargets.insert(unit.getTarget().unit());

			if (isThreatening(unit))
				unit.circleRed();
		}

		// Must see a 3x3 grid of Tiles to set a unit to invalid position
		if (!unit.unit()->exists() && (!unit.isBurrowed() || Commands().overlapsAllyDetection(unit.getPosition()) || Grids().getAGroundCluster(unit.getWalkPosition()) > 0) && unit.getPosition().isValid()) {
			bool move = true;
			for (int x = unit.getTilePosition().x - 1; x < unit.getTilePosition().x + 1; x++) {
				for (int y = unit.getTilePosition().y - 1; y < unit.getTilePosition().y + 1; y++) {
					TilePosition t(x, y);
					if (t.isValid() && !Broodwar->isVisible(t))
						move = false;
				}
			}
			if (move) {
				unit.setPosition(Positions::Invalid);
				unit.setTilePosition(TilePositions::Invalid);
				unit.setWalkPosition(WalkPositions::Invalid);
			}
		}


		// If unit has a valid type, update enemy composition tracking
		if (unit.getType().isValid())
			enemyComposition[unit.getType()] += 1;

		// If unit is not a worker or building, add it to global strength	
		if (!unit.getType().isWorker())
			unit.getType().isFlyer() ? globalEnemyAirStrength += unit.getVisibleAirStrength() : globalEnemyGroundStrength += unit.getVisibleGroundStrength();

		// If a unit is threatening our position
		if (isThreatening(unit) && (unit.getType().groundWeapon().damageAmount() > 0 || unit.getType() == UnitTypes::Terran_Bunker)) {
			if (unit.getType().isBuilding())
				immThreat += max(1.5, unit.getVisibleGroundStrength());
			else
				immThreat += unit.getVisibleGroundStrength();
		}
	}

	// Update myUnits
	for (auto &u : myUnits) {
		auto &unit = u.second;
		if (!unit.unit())
			continue;

		unit.updateUnit();
		updateLocalSimulation(unit);
		updateStrategy(unit);
		updateRole(unit);

		// If unit is not a building and deals damage, add it to global strength	
		if (!unit.getType().isBuilding())
			unit.getType().isFlyer() ? globalAllyAirStrength += unit.getVisibleAirStrength() : globalAllyGroundStrength += unit.getVisibleGroundStrength();

		for (auto p : Terrain().getChokePositions()) {
			auto dist = unit.getPosition().getDistance(p);
			if (dist < 32)
				Commands().addCommand(unit.unit(), p, UnitTypes::None);
		}
	}

	for (auto &u : neutrals) {
		auto &unit = u.second;
		if (!unit.unit() || !unit.unit()->exists())
			continue;
	}
}

void UnitManager::updateLocalSimulation(UnitInfo& unit)
{
	// Variables for calculating local strengths
	auto enemyLocalGroundStrength = 0.0, allyLocalGroundStrength = 0.0;
	auto enemyLocalAirStrength = 0.0, allyLocalAirStrength = 0.0;
	auto unitToEngage = max(0.0, unit.getEngDist() / (24.0 * unit.getSpeed()));
	auto simulationTime = unitToEngage + 5.0;
	auto unitSpeed = unit.getSpeed() * 24.0;
	auto sync = false;

	// If we have excessive resources, ignore our simulation and engage
	if (!ignoreSim && Broodwar->self()->minerals() >= 2000 && Broodwar->self()->gas() >= 2000 && Units().getSupply() >= 380)
		ignoreSim = true;
	if (ignoreSim && Broodwar->self()->minerals() <= 500 || Broodwar->self()->gas() <= 500 || Units().getSupply() <= 240)
		ignoreSim = false;

	if (ignoreSim) {
		unit.setLocalStrategy(1);
		return;
	}

	if (Terrain().isIslandMap() && !unit.getType().isFlyer()) {
		unit.setLocalStrategy(1);
		return;
	}

	// Check every enemy unit being in range of the target
	for (auto &e : enemyUnits) {
		UnitInfo &enemy = e.second;

		// Ignore workers and stasised units
		if (!enemy.unit() || (enemy.getType().isWorker() && !Strategy().enemyRush()) || (enemy.unit() && enemy.unit()->exists() && (enemy.unit()->isStasised() || enemy.unit()->isMorphing())))
			continue;

		// Distance parameters
		auto tempPosition = (enemy.unit()->exists() && enemy.unit()->getOrder() == Orders::AttackUnit) ? enemy.unit()->getOrderTargetPosition() : enemy.getPosition();
		auto widths = (double)enemy.getType().tileWidth() * 16.0 + (double)unit.getType().tileWidth() * 16.0;
		auto enemyRange = (unit.getType().isFlyer() ? enemy.getAirRange() : enemy.getGroundRange());
		auto airDist = tempPosition.getDistance(unit.getPosition());

		// True distance
		auto distance = airDist - enemyRange - widths;

		// Sim values
		auto enemyToEngage = 0.0;
		auto simRatio = 0.0;
		auto speed = enemy.getSpeed() * 24.0;

		// If enemy can move, distance/speed is time to engage
		if (speed > 0.0) {
			enemyToEngage = max(0.0, distance / speed);
			simRatio = max(0.0, simulationTime - enemyToEngage);
		}

		// If enemy can't move, it must be in range of our engage position to be added
		else if (enemy.getPosition().getDistance(unit.getEngagePosition()) - enemyRange - widths <= 0.0) {
			enemyToEngage = max(0.0, distance / unitSpeed);
			simRatio = max(0.0, simulationTime - enemyToEngage);
		}
		else
			continue;

		// Situations where an enemy should be treated as stronger than it actually is
		if (enemy.unit()->exists() && (enemy.unit()->isBurrowed() || enemy.unit()->isCloaked()) && !enemy.unit()->isDetected())
			simRatio = simRatio * 2.0;
		if (!enemy.getType().isFlyer() && Broodwar->getGroundHeight(enemy.getTilePosition()) > Broodwar->getGroundHeight(TilePosition(unit.getEngagePosition())))
			simRatio = simRatio * 2.0;
		if (enemy.getLastVisibleFrame() < Broodwar->getFrameCount())
			simRatio = simRatio * (1.0 + min(1.0, (double(Broodwar->getFrameCount() - enemy.getLastVisibleFrame()) / 1000.0)));

		enemyLocalGroundStrength += enemy.getVisibleGroundStrength() * simRatio;
		enemyLocalAirStrength += enemy.getVisibleAirStrength() * simRatio;
	}

	// Check every ally being in range of the target
	for (auto &a : myUnits) {
		auto &ally = a.second;

		if (!ally.hasTarget() || ally.unit()->isStasised() || ally.unit()->isMorphing())
			continue;

		// Setup distance values
		auto dist = ally.getEngDist();
		auto widths = (double)ally.getType().tileWidth() * 16.0 + (double)ally.getTarget().getType().tileWidth() * 16.0;
		auto speed = (ally.hasTransport() && ally.getTransport().unit()->exists()) ? ally.getTransport().getType().topSpeed() * 24.0 : (24.0 * ally.getSpeed());

		// Setup true distance
		auto distance = max(0.0, dist - widths);

		// HACK: Bunch of hardcoded stuff
		if (ally.getPosition().getDistance(unit.getEngagePosition()) / speed > simulationTime)
			continue;
		if ((ally.getType() == UnitTypes::Protoss_Scout || ally.getType() == UnitTypes::Protoss_Corsair) && ally.getShields() < 30)
			continue;
		if (ally.getType() == UnitTypes::Terran_Wraith && ally.getHealth() <= 100)
			continue;
		if (ally.getPercentShield() < LOW_SHIELD_PERCENT_LIMIT && Broodwar->getFrameCount() < 12000)
			continue;
		if (ally.getType() == UnitTypes::Zerg_Mutalisk && Grids().getEAirThreat((WalkPosition)ally.getEngagePosition()) * 5.0 > ally.getHealth() && ally.getHealth() <= 30)
			continue;

		auto allyToEngage = max(0.0, (distance / speed));
		auto simRatio = max(0.0, simulationTime - allyToEngage);

		// Situations where an ally should be treated as stronger than it actually is
		if ((ally.unit()->isCloaked() || ally.unit()->isBurrowed()) && !Commands().overlapsEnemyDetection(ally.getEngagePosition()))
			simRatio = simRatio * 2.0;
		if (!ally.getType().isFlyer() && Broodwar->getGroundHeight(TilePosition(ally.getEngagePosition())) > Broodwar->getGroundHeight(TilePosition(ally.getTarget().getPosition())))
			simRatio = simRatio * 2.0;

		if (!sync && simRatio > 0.0 && ((unit.getType().isFlyer() && !ally.getType().isFlyer()) || (!unit.getType().isFlyer() && ally.getType().isFlyer())))
			sync = true;

		// We want to fight in open areas in PvT and PvP
		if (Players().vP() || Players().vT())
			simRatio = simRatio * unit.getSimBonus();

		allyLocalGroundStrength += ally.getVisibleGroundStrength() * simRatio;
		allyLocalAirStrength += ally.getVisibleAirStrength() * simRatio;
	}

	double airair = enemyLocalAirStrength > 0.0 ? allyLocalAirStrength / enemyLocalAirStrength : 10.0;
	double airgrd = enemyLocalGroundStrength > 0.0 ? allyLocalAirStrength / enemyLocalGroundStrength : 10.0;
	double grdair = enemyLocalAirStrength > 0.0 ? allyLocalGroundStrength / enemyLocalAirStrength : 10.0;
	double grdgrd = enemyLocalGroundStrength > 0.0 ? allyLocalGroundStrength / enemyLocalGroundStrength : 10.0;

	if (unit.hasTarget()) {

		if (unit.getPosition().getDistance(unit.getSimPosition()) > 720.0)
			unit.setLocalStrategy(0);

		// If simulations need to be synchdd
		else if (sync && !unit.getTarget().getType().isFlyer()) {

			if (grdair >= maxThreshold && grdgrd >= maxThreshold)
				unit.setLocalStrategy(1);
			else if (unit.getType().isFlyer() && enemyLocalAirStrength == 0.0)
				unit.setLocalStrategy(1);
			else if (grdair < minThreshold && grdgrd < minThreshold)
				unit.setLocalStrategy(0);

			Display().displaySim(unit, (grdgrd + grdair) / 2.0);
			unit.setSimValue((grdgrd + grdair) / 2.0);
		}

		else if (sync && unit.getTarget().getType().isFlyer()) {

			if (airgrd >= maxThreshold && airair >= maxThreshold)
				unit.setLocalStrategy(1);
			else if (!unit.getType().isFlyer() && enemyLocalGroundStrength == 0.0)
				unit.setLocalStrategy(1);
			else if (airgrd < minThreshold && airair < minThreshold)
				unit.setLocalStrategy(0);

			Display().displaySim(unit, (airgrd + airair) / 2.0);
			unit.setSimValue((airgrd + airair) / 2.0);
		}

		else if (unit.getTarget().getType().isFlyer()) {
			if (unit.getType().isFlyer()) {

				if (airair >= maxThreshold)
					unit.setLocalStrategy(1);
				else if (airair < minThreshold)
					unit.setLocalStrategy(0);

				Display().displaySim(unit, airair);
				unit.setSimValue(airair);
			}
			else {

				if (airgrd >= maxThreshold)
					unit.setLocalStrategy(1);
				else if (airgrd < minThreshold)
					unit.setLocalStrategy(0);

				Display().displaySim(unit, airgrd);
				unit.setSimValue(airgrd);
			}
		}
		else
		{
			if (unit.getType().isFlyer()) {

				if (grdair >= maxThreshold)
					unit.setLocalStrategy(1);
				else if (grdair < minThreshold)
					unit.setLocalStrategy(0);

				Display().displaySim(unit, grdair);
				unit.setSimValue(grdair);
			}
			else {

				if (grdgrd >= maxThreshold)
					unit.setLocalStrategy(1);
				else if (grdgrd < minThreshold)
					unit.setLocalStrategy(0);

				Display().displaySim(unit, grdgrd);
				unit.setSimValue(grdgrd);
			}
		}
	}
}

void UnitManager::updateStrategy(UnitInfo& unit)
{
	// HACK: Strategy radius 
	double radius = 640.0;

	// Global strategy
	if (Broodwar->self()->getRace() == Races::Protoss) {
		if ((!BuildOrder().isFastExpand() && Strategy().enemyFastExpand())
			|| (Strategy().enemyProxy() && !Strategy().enemyRush())
			|| BuildOrder().isRush())
			unit.setGlobalStrategy(1);

		else if ((Strategy().enemyRush() && !Players().vT())
			|| (!Strategy().enemyRush() && BuildOrder().isHideTech() && BuildOrder().isOpener())
			|| unit.getType().isWorker()
			|| (Broodwar->getFrameCount() < 15000 && BuildOrder().isPlayPassive())
			|| (unit.getType() == UnitTypes::Protoss_Corsair && !BuildOrder().firstReady() && globalEnemyAirStrength > 0.0))
			unit.setGlobalStrategy(0);
		else
			unit.setGlobalStrategy(1);
	}
	else if (Broodwar->self()->getRace() == Races::Terran) {
		if (unit.getType() == UnitTypes::Terran_Vulture)
			unit.setGlobalStrategy(1);
		else if (!BuildOrder().firstReady())
			unit.setGlobalStrategy(0);
		else if (BuildOrder().isHideTech() && BuildOrder().isOpener())
			unit.setGlobalStrategy(0);
		else
			unit.setGlobalStrategy(1);
	}
	else if (Broodwar->self()->getRace() == Races::Zerg) {
		if (BuildOrder().getCurrentBuild() == "ZLurkerTurtle")
			unit.setGlobalStrategy(0);
		else
			unit.setGlobalStrategy(1);
	}
	

	if (unit.hasTarget()) {

		auto fightingAtHome = ((Terrain().isInAllyTerritory(unit.getTilePosition()) && Util().unitInRange(unit)) || Terrain().isInAllyTerritory(unit.getTarget().getTilePosition()));
		auto invisTarget = unit.getTarget().unit() && (unit.getTarget().unit()->isCloaked() || unit.getTarget().isBurrowed()) && !unit.getTarget().unit()->isDetected() && unit.getPosition().getDistance(unit.getTarget().getPosition()) <= unit.getTarget().getGroundRange() + 160;

		// Force engaging
		if (!invisTarget && (Units().isThreatening(unit.getTarget())
			|| (fightingAtHome && (!unit.getType().isFlyer() || !unit.getTarget().getType().isFlyer()) && (Strategy().defendChoke() || unit.getGroundRange() > 64.0))))
			unit.setEngage();

		// Force retreating
		else if ((unit.getType().isMechanical() && unit.getPercentTotal() < LOW_MECH_PERCENT_LIMIT)
			|| (unit.getType() == UnitTypes::Protoss_High_Templar && unit.getEnergy() < 75)
			|| Commands().isInDanger(unit)
			|| Grids().getESplash(unit.getWalkPosition()) > 0
			|| unit.getGlobalStrategy() == 0
			|| invisTarget)
			unit.setRetreat();

		// Close enough to make a decision
		else if (unit.getPosition().getDistance(unit.getSimPosition()) <= radius) {

			// Retreat
			if ((unit.getType() == UnitTypes::Protoss_Zealot && Broodwar->self()->getUpgradeLevel(UpgradeTypes::Leg_Enhancements) == 0 && !BuildOrder().isProxy()  && unit.getTarget().getType() == UnitTypes::Terran_Vulture && Grids().getMobility(unit.getTarget().getWalkPosition()) > 6 && Grids().getCollision(unit.getTarget().getWalkPosition()) < 2)
				|| ((unit.getType() == UnitTypes::Protoss_Scout || unit.getType() == UnitTypes::Protoss_Corsair) && unit.getTarget().getType() == UnitTypes::Zerg_Overlord && Grids().getEAirThreat((WalkPosition)unit.getEngagePosition()) * 5.0 > (double)unit.getShields())
				|| (unit.getType() == UnitTypes::Protoss_Corsair && unit.getTarget().getType() == UnitTypes::Zerg_Scourge && Broodwar->self()->completedUnitCount(UnitTypes::Protoss_Corsair) < 6)
				|| (unit.getType() == UnitTypes::Terran_Medic && unit.unit()->getEnergy() <= TechTypes::Healing.energyCost())
				|| (unit.getType() == UnitTypes::Zerg_Mutalisk && Grids().getEAirThreat((WalkPosition)unit.getEngagePosition()) * 5.0 > unit.getHealth() && unit.getHealth() <= 30)
				|| (unit.getPercentShield() < LOW_SHIELD_PERCENT_LIMIT && Broodwar->getFrameCount() < 12000)
				|| (unit.getType() == UnitTypes::Terran_SCV && Broodwar->getFrameCount() > 12000))
				unit.setRetreat();

			// Engage
			else if (unit.getTarget().unit()->exists() &&
				(((unit.getTarget().getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode || unit.getTarget().getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode) && unit.getPosition().getDistance(unit.getTarget().getPosition()) < 96.0)
					|| ((unit.unit()->isCloaked() || unit.isBurrowed()) && !Commands().overlapsEnemyDetection(unit.getEngagePosition()))
					|| (unit.getType() == UnitTypes::Protoss_Reaver && !unit.unit()->isLoaded() && Util().unitInRange(unit))
					|| (unit.getGlobalStrategy() == 1 && unit.getLocalStrategy() == 1)))
				unit.setEngage();

			// Default to retreat
			else if (unit.getGlobalStrategy() == 0 || unit.getLocalStrategy() == 0)
				unit.setRetreat();
		}
	}
}

void UnitManager::updateRole(UnitInfo& unit)
{
	// Update default role
	if (unit.getRole() == Role::None) {
		if (unit.getType().isWorker())
			unit.setRole(Role::Working);
		else if (unit.getType().isBuilding() && unit.getGroundDamage() == 0.0 && unit.getAirDamage() == 0.0)
			unit.setRole(Role::Producing);
		else if (unit.getType().spaceProvided() > 0)
			unit.setRole(Role::Transporting);
		else
			unit.setRole(Role::Fighting);
	}

	// Check if workers should fight or work
	if (unit.getType().isWorker()) {
		if (unit.getRole() == Role::Working && (Util().reactivePullWorker(unit) || Util().proactivePullWorker(unit) || Util().pullRepairWorker(unit)))
			unit.setRole(Role::Fighting);
		else if (unit.getRole() == Role::Fighting)
			unit.setRole(Role::Working);
	}
}

int UnitManager::roleCount(Role role)
{
	// Find every unit we own with the role we requested
	auto cnt = 0;
	for (auto &u : myUnits) {
		auto &unit = u.second;
		if (unit.getRole() == role)
			cnt++;
	}
	return cnt;
}

bool UnitManager::isThreatening(UnitInfo& unit)
{
	if ((unit.isBurrowed() || (unit.unit() && unit.unit()->exists() && unit.unit()->isCloaked())) && !Commands().overlapsAllyDetection(unit.getPosition()))
		return false;

	if (unit.unit() && unit.unit()->exists() && unit.getType().isWorker() && !unit.unit()->isAttacking() && unit.unit()->getOrder() != Orders::AttackUnit && unit.unit()->getOrder() != Orders::PlaceBuilding && !unit.unit()->isConstructing())
		return false;

	// If unit is hitting a building
	if (unit.hasAttackedRecently() && Terrain().isInAllyTerritory(unit.getTilePosition()) && unit.unit()->exists() && unit.hasTarget() && unit.getTarget().getType().isBuilding())
		return true;

	// If unit is near our main mineral gathering - TODO: Why do I still use this
	if (unit.getPosition().getDistance(Terrain().getMineralHoldPosition()) <= 320.0)
		return true;

	// If unit is near a battery
	if (Broodwar->self()->completedUnitCount(UnitTypes::Protoss_Shield_Battery) > 0) {
		auto battery = Util().getClosestUnit(unit.getPosition(), Broodwar->self(), UnitTypes::Protoss_Shield_Battery);
		if (battery && unit.getPosition().getDistance(battery->getPosition()) <= 128.0)
			return true;		
	}

	if (unit.getType().isBuilding() && Terrain().isInAllyTerritory(unit.getTilePosition()))
		return true;

	// If unit is constructing a bunker near our defend position
	if (unit.unit() && unit.unit()->exists() && unit.unit()->isConstructing() && unit.unit()->getBuildUnit() && unit.unit()->getBuildUnit()->getType() == UnitTypes::Terran_Bunker && unit.getPosition().getDistance(Terrain().getDefendPosition()) <= 640)
		return true;

	// If we're not defending a wall at the natural, check how close the enemy is
	if (!Terrain().getNaturalWall() && !Terrain().isDefendNatural() && !unit.getType().isWorker()) {
		if (unit.getPosition().getDistance(Terrain().getDefendPosition()) <= max(64.0, unit.getGroundRange() / 2)
			|| unit.getPosition().getDistance(Terrain().getDefendPosition()) <= max(64.0, unit.getAirRange() / 2)
			|| (Strategy().defendChoke() && !unit.getType().isFlyer() && Terrain().isInAllyTerritory(unit.getTilePosition())))
			return true;
	}

	// If unit is close to any piece our wall
	else if (Terrain().getNaturalWall()) {
		if (!Strategy().enemyBust()) {
			for (auto &piece : Terrain().getNaturalWall()->largeTiles()) {
				Position center = Position(piece) + Position(64, 48);
				if (unit.getType().isBuilding() && unit.getPosition().getDistance(center) < 480)
					return true;
				if (unit.hasAttackedRecently() && unit.getPosition().getDistance(center) < unit.getGroundRange() + 96.0)
					return true;
			}
			for (auto &piece : Terrain().getNaturalWall()->mediumTiles()) {
				Position center = Position(piece) + Position(48, 32);
				if (unit.getType().isBuilding() && unit.getPosition().getDistance(center) < 480)
					return true;
				if (unit.hasAttackedRecently() && unit.getPosition().getDistance(center) < unit.getGroundRange() + 64.0)
					return true;
			}
			for (auto &piece : Terrain().getNaturalWall()->smallTiles()) {
				Position center = Position(piece) + Position(32, 32);
				if (unit.getType().isBuilding() && unit.getPosition().getDistance(center) < 480)
					return true;
				if (unit.hasAttackedRecently() && unit.getPosition().getDistance(center) < unit.getGroundRange() + 32.0)
					return true;
			}
		}
		else {
			for (auto &piece : Terrain().getNaturalWall()->getDefenses()) {
				Position center = Position(piece) + Position(32, 32);
				if (unit.getPosition().getDistance(center) < unit.getGroundRange())
					return true;
			}
		}
	}
	return false;
}

int UnitManager::getEnemyCount(UnitType t)
{
	map<UnitType, int>::iterator itr = enemyComposition.find(t);
	if (itr != enemyComposition.end())
		return itr->second;
	return 0;
}

void UnitManager::storeUnit(Unit unit)
{
	auto &info = unit->getPlayer() == Broodwar->self() ? myUnits[unit] : (unit->getPlayer() == Broodwar->enemy() ? enemyUnits[unit] : allyUnits[unit]);
	info.setUnit(unit);
	info.updateUnit();
}