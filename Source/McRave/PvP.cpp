#include "McRave.h"
#include "BuildOrder.h"

using namespace BWAPI;
using namespace std;
using namespace UnitTypes;
using namespace McRave::BuildOrder::All;
#define s Units::getSupply()

namespace McRave::BuildOrder::Protoss {

    namespace {

        string enemyBuild() { return Strategy::getEnemyBuild(); }

        bool goonRange() {
            return Broodwar->self()->isUpgrading(UpgradeTypes::Singularity_Charge) || Broodwar->self()->getUpgradeLevel(UpgradeTypes::Singularity_Charge);
        }

        bool addGas() {
            return Broodwar->getStartLocations().size() >= 3 ? (s >= 24) : (s >= 24);
        }

        void defaultPvP() {
            hideTech =          false;
            playPassive =       false;
            fastExpand =        false;
            wallNat =           false;
            wallMain =          false;
            delayFirstTech =    false;

            desiredDetection =  UnitTypes::Protoss_Observer;
            firstUnit =         UnitTypes::None;

            firstUpgrade =		UpgradeTypes::Singularity_Charge;
            firstTech =			TechTypes::None;
            scout =				vis(Protoss_Gateway) > 0;
            gasLimit =			INT_MAX;
            zealotLimit =		1;
            dragoonLimit =		INT_MAX;
        }
    }

    void PvP2GateDefensive() {

        auto enemyMoreZealots = com(Protoss_Zealot) <= Units::getEnemyCount(Protoss_Zealot);

        gasLimit =			(com(Protoss_Cybernetics_Core) && s >= 36) ? INT_MAX : 0;
        getOpening =		com(Protoss_Dark_Templar) <= 2 && s < 80;
        playPassive	=		com(Protoss_Dark_Templar) <= 2 && enemyMoreZealots;
        firstUpgrade =		UpgradeTypes::None;
        firstTech =			TechTypes::None;
        fastExpand =		false;

        zealotLimit =		INT_MAX;
        dragoonLimit =		vis(Protoss_Dark_Templar) > 0 ? INT_MAX : 0;
        firstUnit =         Protoss_Dark_Templar;

        itemQueue[Protoss_Nexus] =					Item(1);
        itemQueue[Protoss_Pylon] =					Item((s >= 14) + (s >= 30), (s >= 16) + (s >= 30));
        itemQueue[Protoss_Gateway] =				Item((s >= 20) + (s >= 24) + (s >= 66));
        itemQueue[Protoss_Assimilator] =			Item(s >= 36);
        itemQueue[Protoss_Shield_Battery] =			Item(enemyMoreZealots && vis(Protoss_Zealot) >= 2 && vis(Protoss_Pylon) >= 2);
        itemQueue[Protoss_Cybernetics_Core] =		Item(s >= 40);
    }

    void PvP2Gate()
    {
        defaultPvP();
        zealotLimit = 5;
        proxy = currentOpener == "Proxy";
        wallNat = currentOpener == "Natural";
        scout = Broodwar->getStartLocations().size() >= 3 ? vis(Protoss_Gateway) >= 1 : vis(Protoss_Gateway) >= 2;

        cutWorkers = Production::hasIdleProduction() && s <= 60;

        // Openers
        if (currentOpener == "Proxy") {
            itemQueue[Protoss_Pylon] =					Item((s >= 12), (s >= 16));
            itemQueue[Protoss_Gateway] =				Item((vis(Protoss_Pylon) > 0) + (vis(Protoss_Gateway) > 0), 2 * (s >= 18));
        }
        else if (currentOpener == "Natural") {
            if (Broodwar->getStartLocations().size() >= 3) {
                itemQueue[Protoss_Pylon] =				Item((s >= 14) + (s >= 30), (s >= 16) + (s >= 30));
                itemQueue[Protoss_Gateway] =			Item((vis(Protoss_Pylon) > 0) + (s >= 20), (s >= 18) + (s >= 20));
            }
            else {
                itemQueue[Protoss_Pylon] =				Item((s >= 14) + (s >= 30), (s >= 16) + (s >= 30));
                itemQueue[Protoss_Gateway] =			Item((vis(Protoss_Pylon) > 0) + (vis(Protoss_Gateway) > 0), 2 * (s >= 18));
            }
        }
        else if (currentOpener == "Main") {
            if (Broodwar->getStartLocations().size() >= 3) {
                itemQueue[Protoss_Pylon] =				Item((s >= 16) + (s >= 30));
                itemQueue[Protoss_Gateway] =			Item((s >= 20) + (s >= 24));
            }
            else {
                itemQueue[Protoss_Pylon] =				Item((s >= 16) + (s >= 30));
                itemQueue[Protoss_Gateway] =			Item((vis(Protoss_Pylon) > 0) + (s >= 20));
            }
        }

        // Reactions
        if (!lockedTransition) {

            // Change Transition
            if (Strategy::enemyRush())
                currentTransition = "DT";
            else if (Strategy::enemyPressure() && currentOpener == "Natural")
                currentTransition = "Defensive";

            // Change Opener

            // Change Build
        }

        // Transitions
        if (currentTransition == "DT") {
            // https://liquipedia.net/starcraft/2_Gateway_Dark_Templar_(vs._Protoss)
            if (Strategy::getEnemyBuild() == "2Gate")
                PvP2GateDefensive();
            else {
                lockedTransition =  vis(Protoss_Citadel_of_Adun) > 0;
                getOpening =		s < 80;
                firstUpgrade =      UpgradeTypes::None;
                zealotLimit =       INT_MAX;

                hideTech =			currentOpener == "Main" && com(Protoss_Zealot) < 2;
                firstUnit =			Protoss_Dark_Templar;
                desiredDetection =  UnitTypes::Protoss_Forge;

                itemQueue[Protoss_Nexus] =				Item(1);
                itemQueue[Protoss_Assimilator] =		Item(s >= 44);
                itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 56);
            }
        }
        else if (currentTransition == "Expand") {
            // https://liquipedia.net/starcraft/2_Gate_(vs._Protoss)#10.2F12_Gateway_Expand
            lockedTransition =  vis(Protoss_Nexus) >= 2;
            getOpening =		s < 100;

            delayFirstTech =    true;
            wallNat =           currentOpener == "Natural" || s >= 56;
            desiredDetection =  UnitTypes::Protoss_Forge;

            itemQueue[Protoss_Assimilator] =		Item(s >= 58);
            itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 60);
            itemQueue[Protoss_Forge] =				Item(s >= 70);
            itemQueue[Protoss_Nexus] =				Item(1 + (s >= 56));

            auto cannonCount = int(1 + Units::getEnemyCount(Protoss_Zealot) + Units::getEnemyCount(Protoss_Dragoon)) / 2;
            itemQueue[Protoss_Photon_Cannon] =		Item(com(Protoss_Forge) > 0 ? cannonCount : 0);
        }
        else if (currentTransition == "Reaver") {
            // https://liquipedia.net/starcraft/2_Gate_Reaver_(vs._Protoss)
            lockedTransition =  vis(Protoss_Robotics_Facility) > 0;
            getOpening =		s < 70;

            desiredDetection =  UnitTypes::Protoss_Forge;

            itemQueue[Protoss_Nexus] =				Item(1);
            itemQueue[Protoss_Assimilator] =		Item(s >= 44);
            itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 56);
            itemQueue[Protoss_Robotics_Facility] =	Item(com(Protoss_Dragoon) >= 2);

            // Decide whether to Reaver first or Obs first
            if (com(Protoss_Robotics_Facility) > 0) {
                if (vis(Protoss_Observer) == 0 && (Units::getEnemyCount(UnitTypes::Protoss_Dragoon) <= 2 || enemyBuild() == "1GateDT"))
                    firstUnit = Protoss_Observer;
                else
                    firstUnit = Protoss_Reaver;
            }
        }
        else if (currentTransition == "Defensive") {
            lockedTransition =  vis(Protoss_Citadel_of_Adun) > 0;
            getOpening =		s < 100;

            playPassive =		com(Protoss_Gateway) < 5 && vis(Protoss_Dark_Templar) == 0;
            zealotLimit	=		INT_MAX;
            firstUnit =			Protoss_Dark_Templar;
            wallNat =           s >= 56 || currentOpener == "Natural";
            desiredDetection =  UnitTypes::Protoss_Forge;

            itemQueue[Protoss_Assimilator] =        Item(s >= 64);
            itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 66);
            itemQueue[Protoss_Forge] =				Item(vis(Protoss_Zealot) >= 4);
            itemQueue[Protoss_Nexus] =				Item(1 + (s >= 56));

            auto cannonCount = int(1 + Units::getEnemyCount(Protoss_Zealot) + Units::getEnemyCount(Protoss_Dragoon)) / 2;
            itemQueue[Protoss_Photon_Cannon] =		Item(com(Protoss_Forge) > 0 ? cannonCount : 0);
        }
    }

    void PvP1GateCore()
    {
        // https://liquipedia.net/starcraft/1_Gate_Core_(vs._Protoss)
        defaultPvP();

        firstUpgrade =		vis(Protoss_Dragoon) > 0 ? UpgradeTypes::Singularity_Charge : UpgradeTypes::None;
        firstTech =			TechTypes::None;
        scout =				Broodwar->getStartLocations().size() >= 3 ? vis(Protoss_Gateway) > 0 : vis(Protoss_Pylon) > 0;
        gasLimit =			INT_MAX;


        // Openers
        if (currentOpener == "1Zealot") {
            zealotLimit = vis(Protoss_Cybernetics_Core) > 0 ? max(2, Units::getEnemyCount(UnitTypes::Protoss_Zealot)) : 1;

            itemQueue[Protoss_Nexus] =				Item(1);
            itemQueue[Protoss_Pylon] =				Item((s >= 16) + (s >= 30));
            itemQueue[Protoss_Assimilator] =		Item(s >= 24);
            itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 34);
        }
        else if (currentOpener == "2Zealot") {
            zealotLimit = vis(Protoss_Cybernetics_Core) > 0 ? Units::getEnemyCount(UnitTypes::Protoss_Zealot) : 2;

            itemQueue[Protoss_Nexus] =				Item(1);
            itemQueue[Protoss_Pylon] =				Item((s >= 16) + (s >= 30));
            itemQueue[Protoss_Assimilator] =		Item(s >= 24);
            itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 40);
        }

        if (s >= 60)
            zealotLimit = 0;

        // Reactions
        if (!lockedTransition) {

            // Change Transition
            if (Strategy::enemyRush()) {
                if (vis(Protoss_Cybernetics_Core) == 0)
                    currentTransition = "Defensive";
                else
                    currentTransition = "4Gate";
            }
            else if (enemyBuild() == "P1GateDT")
                currentTransition = "3GateRobo";

            // Change Opener

            // Change Build
        }

        // Transitions
        if (currentTransition == "3GateRobo") {
            lockedTransition =  vis(Protoss_Robotics_Facility) > 0;
            getOpening =        s < 80;
            playPassive =		!Strategy::enemyFastExpand() && com(Protoss_Reaver) == 0;
        
            desiredDetection =  UnitTypes::Protoss_Forge;

            itemQueue[Protoss_Robotics_Facility] =	Item(s >= 52);
            itemQueue[Protoss_Gateway] =			Item((s >= 20) + (2 * (s >= 58)));

            // Decide whether to Reaver first or Obs first
            if (com(Protoss_Robotics_Facility) > 0) {
                if (vis(Protoss_Observer) == 0 && (Units::getEnemyCount(UnitTypes::Protoss_Dragoon) <= 2 || enemyBuild() == "1GateDT"))
                    firstUnit = Protoss_Observer;
                else
                    firstUnit = Protoss_Reaver;
            }
        }
        else if (currentTransition == "Reaver") {
            // http://liquipedia.net/starcraft/1_Gate_Reaver
            lockedTransition =  vis(Protoss_Robotics_Facility) > 0;
            getOpening =		Strategy::enemyPressure() ? vis(Protoss_Reaver) < 3 : s < 70;
            playPassive =		!Strategy::enemyFastExpand() && com(Protoss_Reaver) == 0;

            dragoonLimit =		INT_MAX;
            zealotLimit =		com(Protoss_Robotics_Facility) >= 1 ? 6 : zealotLimit;

            itemQueue[Protoss_Gateway] =				Item((s >= 20) + (s >= 50) + (s >= 62));
            itemQueue[Protoss_Robotics_Facility] =		Item(s >= 70);

            // Decide whether to Reaver first or Obs first
            if (com(Protoss_Robotics_Facility) > 0) {
                if (vis(Protoss_Observer) == 0 && (Units::getEnemyCount(UnitTypes::Protoss_Dragoon) <= 2 || enemyBuild() == "1GateDT"))
                    firstUnit = Protoss_Observer;
                else
                    firstUnit = Protoss_Reaver;
            }
        }
        else if (currentTransition == "4Gate") {
            // https://liquipedia.net/starcraft/4_Gate_Goon_(vs._Protoss)
            lockedTransition =  vis(Protoss_Gateway) >= 4;
            getOpening =        s < 140 && Broodwar->getFrameCount() < 10000;
            playPassive =       !firstReady();

            gasLimit =          INT_MAX;
            zealotLimit =       vis(Protoss_Cybernetics_Core) > 0 ? 2 : 1;
            desiredDetection =  UnitTypes::Protoss_Forge;

            // HACK
            if (Strategy::enemyRush()) {
                auto enemyMoreZealots = com(Protoss_Zealot) <= Units::getEnemyCount(Protoss_Zealot);
                zealotLimit = INT_MAX;
                gasLimit = vis(Protoss_Dragoon) >= 2 ? 3 : 1;
                itemQueue[Protoss_Shield_Battery] =			Item(enemyMoreZealots && vis(Protoss_Zealot) >= 2 && vis(Protoss_Pylon) >= 2);
            }

            itemQueue[Protoss_Gateway] =			Item((s >= 20) + (s >= 54) + (2 * (s >= 62)));
            itemQueue[Protoss_Assimilator] =		Item(s >= 32);
            itemQueue[Protoss_Cybernetics_Core] =	Item(s >= 34);
        }
        else if (currentTransition == "DT") {
            // https://liquipedia.net/starcraft/2_Gate_DT_(vs._Protoss) -- is actually 1 Gate
            lockedTransition =  vis(Protoss_Citadel_of_Adun) > 0;
            getOpening =        s < 80;
            playPassive =       com(Protoss_Dark_Templar) <= 2;

            firstUpgrade =      UpgradeTypes::None;
            hideTech =          true;
            firstUnit =         Protoss_Dark_Templar;
            desiredDetection =  UnitTypes::Protoss_Forge;

            itemQueue[Protoss_Gateway] =			Item((s >= 20) + (vis(Protoss_Templar_Archives) > 0));
        }
        else if (currentTransition == "Defensive") {
            lockedTransition = true;
            desiredDetection =  UnitTypes::Protoss_Forge;

            PvP2GateDefensive();
        }
    }
}