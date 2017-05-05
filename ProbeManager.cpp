#include "ProbeManager.h"
#include "ResourceManager.h"

using namespace BWAPI;
using namespace std;

void ProbeTrackerClass::storeProbe(Unit probe)
{
	ProbeInfo newProbe;
	myProbes[probe] = newProbe;
	return;
}

void ProbeTrackerClass::removeProbe(Unit probe)
{
	//
}

void ProbeTrackerClass::assignProbe(Unit probe)
{
	// Assign a task if none
	int cnt = 1;
	for (auto &gas : ResourceTracker::Instance().getMyGas())
	{
		if (gas.second.getUnitType() == UnitTypes::Protoss_Assimilator && gas.first->isCompleted() && gas.second.getGathererCount() < 3)
		{
			gas.second.setGathererCount(gas.second.getGathererCount() + 1);
			myProbes[probe].setTarget(gas.first);
		}
	}

	// First checks if a mineral field has 0 Probes mining, if none, checks if a mineral field has 1 Probe mining. Assigns to 0 first, then 1. Spreads saturation.
	while (cnt <= 2)
	{
		for (auto &mineral : ResourceTracker::Instance().getMyMinerals())
		{
			if (mineral.second.getGathererCount() < cnt)
			{
				mineral.second.setGathererCount(mineral.second.getGathererCount() + 1);
				myProbes[probe].setTarget(mineral.first);
			}
		}
		cnt++;
	}
	return;
}

void ProbeTrackerClass::update()
{
	for (auto u : Broodwar->self()->getUnits())
	{
		if (u->getType() != UnitTypes::Protoss_Probe)
		{
			continue;
		}
		if (myProbes.find(u) == myProbes.end())
		{
			storeProbe(u);
		}
	}

	// For each Probe mapped to gather minerals
	for (auto &u : myProbes)
	{
		// If no valid target, try to get a new one
		if (!u.second.getTarget())
		{
			assignProbe(u.first);
			// Any idle Probes can gather closest mineral field until they are assigned again
			if (u.first->isIdle() && u.first->getClosestUnit(Filter::IsMineralField))
			{
				u.first->gather(u.first->getClosestUnit(Filter::IsMineralField));
				continue;
			}
			continue;
		}

		// Attack units in mineral line
		if (resourceGrid[u.first->getTilePosition().x][u.first->getTilePosition().y] > 0 && u.first->getUnitsInRadius(64, Filter::IsEnemy).size() > 0 && (u.first->getHitPoints() + u.first->getShields()) > 20)
		{
			u.first->attack(u.first->getClosestUnit(Filter::IsEnemy, 320));
		}
		else if (u.first->getLastCommand().getType() == UnitCommandTypes::Attack_Unit && (resourceGrid[u.first->getTilePosition().x][u.first->getTilePosition().y] == 0 || (u.first->getHitPoints() + u.first->getShields()) <= 20))
		{
			u.first->stop();
		}

		// Else command probe
		if (u.first && u.first->exists())
		{
			// Draw on every frame
			if (u.first && u.second.getTarget())
			{
				Broodwar->drawLineMap(u.first->getPosition(), u.second.getTarget()->getPosition(), Broodwar->self()->getColor());
			}

			// Return if not latency frame
			if (Broodwar->getFrameCount() % Broodwar->getLatencyFrames() != 0)
			{
				continue;
			}

			// If idle and carrying minerals, return cargo			
			if (u.first->isIdle() && u.first->isCarryingMinerals())
			{
				u.first->returnCargo();
				continue;
			}

			// If not scouting and there's boulders to remove
			if (!scouting && ResourceTracker::Instance().getMyBoulders().size() > 0)
			{
				for (auto b : ResourceTracker::Instance().getMyBoulders())
				{
					if (b.first && b.first->exists() && !u.first->isGatheringMinerals() && u.first->getDistance(b.second.getPosition()) < 512)
					{
						u.first->gather(b.first);
						break;
					}
				}
			}

			// If we have been given a command this frame already, continue
			if (u.first->getLastCommandFrame() >= Broodwar->getFrameCount() && (u.first->getLastCommand().getType() == UnitCommandTypes::Move || u.first->getLastCommand().getType() == UnitCommandTypes::Build))
			{
				continue;
			}


			// If idle and not targeting the mineral field the Probe is mapped to
			if (u.first->isIdle() || (u.first->isGatheringMinerals() && !u.first->isCarryingMinerals() && u.first->getTarget() != u.second.getTarget()))
			{
				// If the Probe has a target
				if (u.first->getTarget())
				{
					// If the target has a resource count of 0 (mineral blocking a ramp), let Probe continue mining it
					if (u.first->getTarget()->getResources() == 0)
					{
						continue;
					}
				}
				// If the mineral field is in vision and no target, force to gather from the assigned mineral field
				if (u.second.getTarget() && u.second.getTarget()->exists())
				{
					u.first->gather(u.second.getTarget());
					continue;
				}
			}
		}


		// Crappy scouting method
		if (!scout)
		{
			scout = u.first;
		}
		if (u.first == scout)
		{
			if (supply >= 18 && scouting)
			{
				for (auto start : getStartLocations())
				{
					if (Broodwar->isExplored(start->getTilePosition()) == false)
					{
						u.first->move(start->getPosition());
						break;
					}
				}
			}
			else if (u.first->getUnitsInRadius(256, Filter::IsEnemy && !Filter::IsWorker && Filter::CanAttack).size() > 0)
			{
				u.first->stop();
				scouting = false;
			}
		}
	}
}