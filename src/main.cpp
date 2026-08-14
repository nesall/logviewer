#include <cstdio>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#define STB_IMAGE_IMPLEMENTATION
#include "3rdparty/stb_image.h"

#include "ui/app.h"

namespace {

  void GlfwErrorCallback(int error, const char *description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
  }

} // namespace

int main() {
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit()) {
    return 1;
  }

  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  GLFWwindow *window = glfwCreateWindow(1280, 800, ui::App::appBaseTitle(), nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    return 1;
  }

  GLFWimage images[1]{};
  images[0].pixels = stbi_load("assets/icon.png", &images[0].width, &images[0].height, 0, 4); // RGBA 4 channels
  if (images[0].pixels) {
    glfwSetWindowIcon(window, 1, images);
    stbi_image_free(images[0].pixels);
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImFont *mainFont = io.Fonts->AddFontFromFileTTF("assets/fonts/Rubik-Regular.ttf", 16.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
#include "3rdparty/IconsFontAwesome7.h"
  static const ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
  ImFontConfig fontConfig;
  fontConfig.MergeMode = true;
  fontConfig.PixelSnapH = true;
  fontConfig.OversampleV = 3;
  fontConfig.OversampleH = 1;
  fontConfig.GlyphMinAdvanceX = 16.0f; // Keeps icon widths uniform
  io.Fonts->AddFontFromFileTTF("assets/fonts/Font Awesome 7 Free-Solid-900.otf", 14.0f, &fontConfig, iconsRanges);

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ui::App app(window);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    app.render();

    ImGui::Render();
    int display_w = 0;
    int display_h = 0;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  app.saveSettings();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}