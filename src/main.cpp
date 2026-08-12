#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "game.hpp"
#include "vulkan_context.hpp"

namespace {
GLFWwindow* g_window = nullptr;
VulkanContext* g_vk = nullptr;
Game g_game;
std::vector<float> g_positions, g_colors, g_sizes;

void errorCallback(int, const char* desc) { std::fprintf(stderr, "GLFW error: %s\n", desc); }

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_SPACE: {
            // Select the player champion
            for (size_t i = 0; i < g_game.units.size(); ++i) {
                if (g_game.units[i].type == UnitType::Champion) {
                    g_game.selectedUnit = static_cast<int>(i);
                    break;
                }
            }
            break;
        }
        case GLFW_KEY_R:
            g_game.reset();
            break;
        default:
            break;
    }
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    g_game.zoom -= static_cast<float>(yoffset) * 8.0f;
    if (g_game.zoom < 20.0f) g_game.zoom = 20.0f;
    if (g_game.zoom > 250.0f) g_game.zoom = 250.0f;
}
}  // namespace

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "failed to init GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    g_window = glfwCreateWindow(1280, 720, "Moba - Engine Sandbox", nullptr, nullptr);
    if (!g_window) {
        std::fprintf(stderr, "failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwSetErrorCallback(errorCallback);
    glfwSetKeyCallback(g_window, keyCallback);
    glfwSetScrollCallback(g_window, scrollCallback);

    try {
        g_vk = new VulkanContext(g_window);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan init failed: %s\n", e.what());
        glfwTerminate();
        return 1;
    }

    g_game.init();

    auto last = std::chrono::steady_clock::now();

    g_game.fillInstances(g_positions, g_colors, g_sizes);

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        // Simple pan controls
        if (glfwGetKey(g_window, GLFW_KEY_W) == GLFW_PRESS) g_game.camZ -= 30.0f * dt;
        if (glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS) g_game.camZ += 30.0f * dt;
        if (glfwGetKey(g_window, GLFW_KEY_A) == GLFW_PRESS) g_game.camX -= 30.0f * dt;
        if (glfwGetKey(g_window, GLFW_KEY_D) == GLFW_PRESS) g_game.camX += 30.0f * dt;

        g_game.update(dt);
        g_game.fillInstances(g_positions, g_colors, g_sizes);

        // Orthographic top-down camera
        int fbw = 1280, fbh = 720;
        glfwGetFramebufferSize(g_window, &fbw, &fbh);
        float aspect = (fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh) : 1.0f;
        glm::mat4 proj = glm::ortho(-g_game.zoom * aspect, g_game.zoom * aspect,
                                    -g_game.zoom, g_game.zoom, -100.0f, 100.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(g_game.camX, 60.0f, g_game.camZ),
                                     glm::vec3(g_game.camX, 0.0f, g_game.camZ),
                                     glm::vec3(0.0f, 0.0f, -1.0f));
        glm::mat4 viewProj = proj * view;

        g_vk->draw(glm::value_ptr(viewProj), g_positions.data(), g_colors.data(),
                   g_sizes.data(), static_cast<uint32_t>(g_positions.size() / 2));
    }

    delete g_vk;
    g_vk = nullptr;
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 0;
}
