#pragma once

#include <cstdint>
#include <vector>

// Basic MOBA-style game simulation.
// World space: XZ plane (Y up). Units are colored squares.
enum class UnitType { Minion, Champion, Tower, Nexus };

struct Unit {
    float posX = 0.0f;
    float posZ = 0.0f;
    float targetX = 0.0f;
    float targetZ = 0.0f;
    float speed = 20.0f;
    float r = 0.6f, g = 0.6f, b = 0.6f, a = 1.0f;
    bool alive = true;
    UnitType type = UnitType::Minion;
    int team = 0;  // 0 = blue, 1 = red
};

struct Game {
    // Camera (top-down)
    float camX = 0.0f;
    float camZ = 0.0f;
    float zoom = 60.0f;

    int selectedUnit = -1;

    // Points/lanes
    std::vector<std::pair<float, float>> lanePath;   // path minions follow
    std::vector<Unit> units;

    void init();
    void reset();
    void update(float dt);
    void spawnMinionWave(int team);
    float spawnTimer[2] = {0.0f, 0.0f};
    float spawnInterval = 8.0f;

    // Build instance arrays for rendering
    void fillInstances(std::vector<float>& positions, std::vector<float>& colors,
                       std::vector<float>& sizes) const;
};
