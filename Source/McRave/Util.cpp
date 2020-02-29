#include "McRave.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;;

namespace McRave::Util {

    namespace {
        Time gameTime(0, 0);
        map<const BWEM::ChokePoint*, vector<WalkPosition>> concaveCache;
    }

    bool isTightWalkable(UnitInfo& unit, Position here)
    {
        if (unit.getType().isFlyer() && unit.getRole() != Role::Transport)
            return true;

        const auto w = WalkPosition(here);
        const auto hw = unit.getWalkWidth() / 2;
        const auto hh = unit.getWalkHeight() / 2;

        const auto left = max(0, w.x - hw);
        const auto right = min(1024, w.x + hw + (1 - unit.getWalkWidth() % 2));
        const auto top = max(0, w.y - hh);
        const auto bottom = min(1024, w.y + hh + (1 - unit.getWalkWidth() % 2));

        // Rectangle of current unit position
        const auto topLeft = Position(unit.getWalkPosition());
        const auto botRight = topLeft + Position(unit.getWalkWidth() * 8, unit.getWalkHeight() * 8) + Position(8 * (1 - unit.getWalkWidth() % 2), 8 * (1 - unit.getWalkHeight() % 2));

        for (auto x = left; x < right; x++) {
            for (auto y = top; y < bottom; y++) {
                const WalkPosition w(x, y);
                const auto p = Position(w) + Position(4, 4);

                if (rectangleIntersect(topLeft, botRight, p))
                    continue;
                else if (Grids::getMobility(w) < 1 || Grids::getCollision(w) > 0)
                    return false;
            }
        }
        return true;
    }

    double getCastLimit(TechType tech)
    {
        if (tech == TechTypes::Psionic_Storm || tech == TechTypes::Maelstrom || tech == TechTypes::Plague || tech == TechTypes::Ensnare)
            return 1.5;
        if (tech == TechTypes::Stasis_Field)
            return 2.5;
        return 0.0;
    }

    int getCastRadius(TechType tech)
    {
        if (tech == TechTypes::Psionic_Storm || tech == TechTypes::Stasis_Field || tech == TechTypes::Maelstrom || tech == TechTypes::Plague || tech == TechTypes::Ensnare)
            return 48;
        return 0;
    }

    bool rectangleIntersect(Position topLeft, Position botRight, Position target)
    {
        if (target.x >= topLeft.x
            && target.x < botRight.x
            && target.y >= topLeft.y
            && target.y < botRight.y)
            return true;
        return false;
    }

    bool rectangleIntersect(Position topLeft, Position botRight, int x, int y)
    {
        if (x >= topLeft.x
            && x < botRight.x
            && y >= topLeft.y
            && y < botRight.y)
            return true;
        return false;
    }

    const BWEM::ChokePoint * getClosestChokepoint(Position here)
    {
        double distBest = DBL_MAX;
        const BWEM::ChokePoint * closest = nullptr;

        for (auto &area : mapBWEM.Areas()) {
            for (auto &choke : area.ChokePoints()) {
                double dist = Position(choke->Center()).getDistance(here);
                if (dist < distBest) {
                    distBest = dist;
                    closest = choke;
                }
            }
        }
        return closest;
    }

    int chokeWidth(const BWEM::ChokePoint * choke)
    {
        if (!choke)
            return 0;
        return int(choke->Pos(choke->end1).getDistance(choke->Pos(choke->end2))) * 8;
    }

    Position getConcavePosition(UnitInfo& unit, BWEM::Area const * area, BWEM::ChokePoint const * choke)
    {
        // Force ranged concaves if enemy has ranged units (defending only)
        const auto enemyRangeExists = Players::getTotalCount(PlayerState::Enemy, UnitTypes::Protoss_Dragoon) > 0
            || Players::getTotalCount(PlayerState::Enemy, UnitTypes::Zerg_Hydralisk) > 0
            || Players::vT();

        // Don't try concaves without chokepoints for now (can use lines in future)
        if (!choke)
            return unit.getPosition();

        auto chokeCount = area->ChokePoints().size();
        auto chokeCenter = Position(choke->Center());
        auto isMelee = unit.getGroundDamage() > 0 && unit.getGroundRange() <= 32.0;
        auto meleeCount = com(Protoss_Zealot) + com(Zerg_Zergling) + com(Terran_Firebat);
        auto rangedCount = com(Protoss_Dragoon) + com(Protoss_Reaver) + com(Protoss_High_Templar) + com(Zerg_Hydralisk) + com(Terran_Marine) + com(Terran_Medic) + com(Terran_Siege_Tank_Siege_Mode) + com(Terran_Siege_Tank_Tank_Mode) + com(Terran_Vulture);
        auto base = area->Bases().empty() ? nullptr : &area->Bases().front();
        auto scoreBest = 0.0;
        auto posBest = unit.getPosition();
        auto line = BWEB::Map::lineOfBestFit(choke);

        auto useMeleeRadius = isMelee && !enemyRangeExists && Players::getSupply(PlayerState::Self) < 80;
        auto radius = useMeleeRadius ? 64.0 : max(64.0, (meleeCount * 16.0) + (Util::chokeWidth(choke) / 2.0));
        auto alreadyValid = false;

        // Check if a wall exists at this choke
        auto wall = BWEB::Walls::getWall(area, choke);
        if (wall) {
            auto minDistance = DBL_MAX;
            for (auto &piece : wall->getLargeTiles()) {
                auto center = Position(piece) + Position(64, 48);
                auto closestGeo = BWEB::Map::getClosestChokeTile(choke, center);
                if (center.getDistance(closestGeo) < minDistance)
                    minDistance = center.getDistance(closestGeo);
            }

            for (auto &piece : wall->getMediumTiles()) {
                auto center = Position(piece) + Position(48, 32);
                auto closestGeo = BWEB::Map::getClosestChokeTile(choke, center);
                if (center.getDistance(closestGeo) < minDistance)
                    minDistance = center.getDistance(closestGeo);
            }

            for (auto &piece : wall->getSmallTiles()) {
                auto center = Position(piece) + Position(32, 32);
                auto closestGeo = BWEB::Map::getClosestChokeTile(choke, center);
                if (center.getDistance(closestGeo) < minDistance)
                    minDistance = center.getDistance(closestGeo);
            }
            radius = minDistance + (32.0 * wall->getOpening().isValid());
        }

        const auto scorePosition = [&](WalkPosition w) {
            const auto t = TilePosition(w);
            const auto p = Position(w);

            // Find a vector projection of this point
            auto projection = vectorProjection(line, p);
            auto projDist = projection.getDistance(chokeCenter);

            // Choke end nodes and distance to choke center
            auto pt1 = Position(choke->Pos(choke->end1));
            auto pt2 = Position(choke->Pos(choke->end2));
            auto pt1Dist = pt1.getDistance(chokeCenter);
            auto pt2Dist = pt2.getDistance(chokeCenter);

            // Determine if we should lineup at projection or wrap around choke end nodes
            if (chokeCount < 3 && (pt1Dist < projDist || pt2Dist < projDist))
                projection = pt1.getDistance(projection) < pt2.getDistance(projection) ? pt1 : pt2;

            if ((alreadyValid && p.getDistance(unit.getPosition()) > 160.0)
                || p.getDistance(projection) < radius
                || p.getDistance(projection) >= 640.0
                || Buildings::overlapsQueue(unit, p)
                || !Broodwar->isWalkable(w)
                || !Util::isTightWalkable(unit, p))
                return 0.0;

            const auto distProj = exp(p.getDistance(projection));
            const auto distCenter = p.getDistance(chokeCenter);
            const auto distUnit = p.getDistance(unit.getPosition());
            const auto distAreaBase = base ? base->Center().getDistance(p) : 1.0;
            return 1.0 / (distCenter * distAreaBase * distUnit * distProj);
        };

        // Testing something
        alreadyValid = scorePosition(unit.getWalkPosition()) > 0.0 && find(concaveCache[choke].begin(), concaveCache[choke].end(), unit.getWalkPosition()) != concaveCache[choke].end();

        // Find a position around the center that is suitable        
        auto &tiles = concaveCache[choke];
        for (auto &w : tiles) {
            const auto score = scorePosition(w);

            if (score > scoreBest) {
                posBest = Position(w);
                scoreBest = score;
            }
        }
        return posBest;
    }

    Position getInterceptPosition(UnitInfo& unit)
    {
        // If we can't see the units speed, return its current position
        if (!unit.getTarget().unit()->exists() || unit.getSpeed() == 0.0 || unit.getTarget().getSpeed() == 0.0)
            return unit.getTarget().getPosition();

        auto timeToEngage = clamp((unit.getEngDist() / unit.getSpeed()) * unit.getTarget().getSpeed() / unit.getSpeed(), 0.0, 150.0);
        auto targetDestination = unit.getTarget().getPosition() + Position(int(unit.getTarget().unit()->getVelocityX() * timeToEngage), int(unit.getTarget().unit()->getVelocityY() * timeToEngage));
        targetDestination = Util::clipPosition(targetDestination);
        return targetDestination;
    }

    Position clipLine(Position source, Position target)
    {
        if (target.isValid())
            return target;

        auto sqDist = source.getApproxDistance(target);
        auto clip = clipPosition(target);
        auto dx = clip.x - target.x;
        auto dy = clip.y - target.y;

        if (abs(dx) < abs(dy)) {
            int y = (int)sqrt(sqDist - dx * dx);
            target.x = clip.x;
            if (source.y - y < 0)
                target.y = source.y + y;
            else if (source.y + y >= Broodwar->mapHeight() * 32)
                target.y = source.y - y;
            else
                target.y = (target.y >= source.y) ? source.y + y : source.y - y;
        }
        else {
            int x = (int)sqrt(sqDist - dy * dy);
            target.y = clip.y;

            if (source.x - x < 0)
                target.x = source.x + x;
            else if (source.x + x >= Broodwar->mapWidth() * 32)
                target.x = source.x - x;
            else
                target.x = (target.x >= source.x) ? source.x + x : source.x - x;
        }
        return target;
    }

    Position clipPosition(Position source)
    {
        source.x = clamp(source.x, 0, Broodwar->mapWidth() * 32);
        source.y = clamp(source.y, 0, Broodwar->mapHeight() * 32);
        return source;
    }

    Position vectorProjection(pair<Position, Position> line, Position here)
    {
        auto directionVector = line.second - line.first;
        auto currVector = here - line.first;
        auto projCalc = ((directionVector.x * currVector.x) + (directionVector.y * currVector.y)) / (pow(directionVector.x, 2.0) + pow(directionVector.y, 2.0));
        return (line.first + Position(int(projCalc * directionVector.x), int(projCalc * directionVector.y)));
    }

    Time getTime()
    {
        return gameTime;
    }

    void onStart()
    {
        const auto createCache = [&](const BWEM::ChokePoint * chokePoint, const BWEM::Area * area) {
            auto center = chokePoint->Center();
            for (int x = center.x - 60; x <= center.x + 60; x++) {
                for (int y = center.y - 60; y <= center.y + 60; y++) {
                    WalkPosition w(x, y);
                    const auto p = Position(w) + Position(4, 4);

                    if (!p.isValid()
                        || (area && mapBWEM.GetArea(w) != area)
                        || Grids::getMobility(w) < 6)
                        continue;

                    auto closest = getClosestChokepoint(p);
                    if (closest != chokePoint && p.getDistance(Position(closest->Center())) < 160.0 && (closest == BWEB::Map::getMainChoke() || closest == BWEB::Map::getNaturalChoke()))
                        continue;

                    concaveCache[chokePoint].push_back(w);
                }
            }
        };

        // Main area for defending sometimes is wrong like Andromeda
        const BWEM::Area * defendArea = nullptr;
        auto &[a1, a2] = BWEB::Map::getMainChoke()->GetAreas();
        if (a1 && Terrain::isInAllyTerritory(a1))
            defendArea = a1;
        if (a2 && Terrain::isInAllyTerritory(a2))
            defendArea = a2;

        createCache(BWEB::Map::getMainChoke(), defendArea);
        createCache(BWEB::Map::getMainChoke(), BWEB::Map::getMainArea());

        // Natural area should always be correct
        createCache(BWEB::Map::getNaturalChoke(), BWEB::Map::getNaturalArea());
    }

    void onFrame()
    {
        if (Broodwar->getFrameCount() % 24 == 0) {
            gameTime.seconds++;
            if (gameTime.seconds >= 60) {
                gameTime.seconds = 0;
                gameTime.minutes++;
            }
        }
    }
}