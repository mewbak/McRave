#pragma once
#include <BWAPI.h>
#include "BWEB.h"
#include "McRave.h"

namespace McRave
{
    class ResourceInfo
    {
    private:
        int gathererCount;
        int remainingResources;
        BWAPI::Unit thisUnit;
        BWAPI::UnitType type;
        BWAPI::Position position;
        BWAPI::TilePosition tilePosition;
        const BWEB::Stations::Station * station;
        ResourceState rState;
    public:
        ResourceInfo() {
            station = nullptr;
            thisUnit = nullptr;
            gathererCount = 0;
            remainingResources = 0;
            rState = ResourceState::None;
            type = BWAPI::UnitTypes::None;
            position = BWAPI::Positions::None;
            tilePosition = BWAPI::TilePositions::None;
        }

        void updateResource() {
            type				= thisUnit->getType();
            remainingResources	= thisUnit->getResources();
            position			= thisUnit->getPosition();
            tilePosition		= thisUnit->getTilePosition();
        }

        bool hasStation() { return station != nullptr; }
        const BWEB::Stations::Station * getStation() { return station; }
        void setStation(const BWEB::Stations::Station* newStation) { station = newStation; }

        int getGathererCount() { return gathererCount; };
        int getRemainingResources() { return remainingResources; }
        ResourceState getResourceState() { return rState; }
        BWAPI::Unit unit() { return thisUnit; }
        BWAPI::UnitType getType() { return type; }
        BWAPI::Position getPosition() { return position; }
        BWAPI::TilePosition getTilePosition() { return tilePosition; }

        void setGathererCount(int newInt) { gathererCount = newInt; }
        void setRemainingResources(int newInt) { remainingResources = newInt; }
        void setResourceState(ResourceState newState) { rState = newState; }
        void setUnit(BWAPI::Unit newUnit) { thisUnit = newUnit; }
        void setType(BWAPI::UnitType newType) { type = newType; }
        void setPosition(BWAPI::Position newPosition) { position = newPosition; }
        void setTilePosition(BWAPI::TilePosition newTilePosition) { tilePosition = newTilePosition; }
    };
}
