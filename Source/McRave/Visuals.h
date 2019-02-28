#pragma once
#include <BWAPI.h>

namespace McRave::Visuals {

    void onFrame();
    void startPerfTest();
    void endPerfTest(std::string);
    void onSendText(std::string);
    void displayPath(std::vector<BWAPI::TilePosition>);


    void drawDebugText(std::string, double);
    void drawDebugText(std::string, int);
};