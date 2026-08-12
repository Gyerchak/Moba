#include "game.hpp"

#include <algorithm>
#include <cmath>

namespace {
float length(float x, float z) { return std::sqrt(x * x + z * z); }
float dist(const Unit& a, float x, float z) {
    return length(a.posX - x, a.posZ - z);
}
}  // namespace

void Game::init() {
    lanePath = {
        {-30.0f, 30.0f},
        {-10.0f, 20.0f},
        {0.0f, 0.0f},
        {10.0f, -20.0f},
        {30.0f, -30.0f},
    };

    reset();
}

void Game::reset() {
    units.clear();
    selectedUnit = -1;
    spawnTimer[0] = 0.0f;
    spawnTimer[1] = 0.0f;

    // Towers at both ends of each lane (+ tell unit to guard near towers)
    auto tower = [&](float x, float z, int team, float r, float g, float b) {
        Unit u;
        u.type = UnitType::Tower;
        u.posX = x;
        u.posZ = z;
        u.targetX = x;
        u.targetZ = z;
        u.speed = 0.0f;
        u.r = r;
        u.g = g;
        u.b = b;
        u.team = team;
        u.alive = true;
        units.push_back(u);
    };

    // Blue (top-left) vs Red (bottom-right)
    tower(-40.0f, 40.0f, 0, 0.2f, 0.4f, 0.9f);
    tower(-20.0f, 20.0f, 0, 0.2f, 0.4f, 0.9f);
    tower(40.0f, -40.0f, 1, 0.9f, 0.3f, 0.2f);
    tower(20.0f, -20.0f, 1, 0.9f, 0.3f, 0.2f);

    Unit nexus;
    nexus.type = UnitType::Nexus;
    nexus.posX = -62.0f;
    nexus.posZ = 62.0f;
    nexus.targetX = -62.0f;
    nexus.targetZ = 62.0f;
    nexus.speed = 0.0f;
    nexus.r = 0.1f;
    nexus.g = 0.3f;
    nexus.b = 0.7f;
    nexus.team = 0;
    units.push_back(nexus);

    nexus.posX = 62.0f;
    nexus.posZ = -62.0f;
    nexus.targetX = 62.0f;
    nexus.targetZ = -62.0f;
    nexus.team = 1;
    nexus.r = 0.7f;
    nexus.g = 0.2f;
    nexus.b = 0.1f;
    units.push_back(nexus);

    // A champion the player controls
    Unit champ;
    champ.type = UnitType::Champion;
    champ.posX = -10.0f;
    champ.posZ = 30.0f;
    champ.targetX = -10.0f;
    champ.targetZ = 30.0f;
    champ.speed = 45.0f;
    champ.r = 0.2f;
    champ.g = 0.9f;
    champ.b = 0.3f;
    champ.team = 0;
    units.push_back(champ);

    spawnMinionWave(0);
    spawnMinionWave(1);
}

void Game::spawnMinionWave(int team) {
    // Spawn a wave of minions starting at each team's side marching down lane.
    const int count = 5;
    float baseX = (team == 0) ? -30.0f : 30.0f;
    float baseZ = (team == 0) ? 30.0f : -30.0f;
    float dirX = (team == 0) ? 1.0f : -1.0f;
    float dirZ = (team == 0) ? -1.0f : 1.0f;

    for (int i = 0; i < count; ++i) {
        Unit u;
        u.type = UnitType::Minion;
        u.posX = baseX + dirX * (1.5f * i);
        u.posZ = baseZ + dirZ * (1.5f * i) * 0.5f;
        u.targetX = u.posX;
        u.targetZ = u.posZ;
        u.speed = 22.0f;
        u.team = team;
        if (team == 0) {
            u.r = 0.3f;
            u.g = 0.6f;
            u.b = 1.0f;
        } else {
            u.r = 1.0f;
            u.g = 0.4f;
            u.b = 0.3f;
        }
        units.push_back(u);
    }
}

void Game::update(float dt) {
    // Camera follows selected champion
    if (selectedUnit >= 0 && selectedUnit < static_cast<int>(units.size())) {
        camX = units[selectedUnit].posX;
        camZ = units[selectedUnit].posZ;
    }

    // Spawn minion waves periodically
    for (int t = 0; t < 2; ++t) {
        spawnTimer[t] += dt;
        if (spawnTimer[t] >= spawnInterval) {
            spawnTimer[t] = 0.0f;
            if (int(units.size()) < 1500) spawnMinionWave(t);
        }
    }

    // March units toward their target
    for (auto& u : units) {
        if (u.speed <= 0.0f) continue;
        float dx = u.targetX - u.posX;
        float dz = u.targetZ - u.posZ;
        if (u.type == UnitType::Minion) {
            // march along lane path: follow lanePath waypoints
            float closest = 1e9f;
            int idx = -1;
            for (size_t i = 0; i < lanePath.size(); ++i) {
                float d = dist(u, lanePath[i].first, lanePath[i].second);
                if (d < closest) {
                    closest = d;
                    idx = static_cast<int>(i);
                }
            }
            // move toward next waypoint in order
            int next = (u.team == 0) ? idx + 1 : idx - 1;
            if (next >= 0 && next < static_cast<int>(lanePath.size())) {
                dx = lanePath[next].first - u.posX;
                dz = lanePath[next].second - u.posZ;
            } else {
                dx = 0;
                dz = 0;
            }
        }
        float len = length(dx, dz);
        if (len > 0.001f) {
            float step = u.speed * dt;
            if (step > len) step = len;
            u.posX += dx / len * step;
            u.posZ += dz / len * step;
        }
    }
}

void Game::fillInstances(std::vector<float>& positions,
                         std::vector<float>& colors,
                         std::vector<float>& sizes) const {
    positions.clear();
    colors.clear();
    sizes.clear();
    for (const auto& u : units) {
        if (!u.alive) continue;
        float size = 1.0f;
        if (u.type == UnitType::Champion) size = 2.0f;
        if (u.type == UnitType::Tower) size = 3.0f;
        if (u.type == UnitType::Nexus) size = 5.0f;
        positions.push_back(u.posX);
        positions.push_back(u.posZ);
        colors.push_back(u.r);
        colors.push_back(u.g);
        colors.push_back(u.b);
        colors.push_back(u.a);
        sizes.push_back(size);
    }
}
