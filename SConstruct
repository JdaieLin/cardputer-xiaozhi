import os

build_dir = "build"
app_name = "xiaozhi_app"
sim_name = "xiaozhi_simulator"

env = Environment(
    CXX="g++",
    CC="gcc",
    CXXFLAGS=["-std=c++17", "-Wall", "-Wextra", "-O2"],
    CPPPATH=["main/include"],
)

common_sources = [
    "main/src/application.cpp",
    "main/src/audio_pipeline.cpp",
    "main/src/config.cpp",
    "main/src/hal_stub.cpp",
    "main/src/ota_client.cpp",
    "main/src/ui.cpp",
    "main/src/ws_client.cpp",
]

console_sources = common_sources + ["main/src/main.cpp"]

if not os.path.exists(build_dir):
    os.makedirs(build_dir)

app_target = env.Program(target=os.path.join(build_dir, app_name), source=console_sources)

sim_env = env.Clone()
sim_sources = [
    "main/src/application.cpp",
    "main/src/audio_pipeline.cpp",
    "main/src/audio_pipeline_sdl.cpp",
    "main/src/config.cpp",
    "main/src/hal_sdl.cpp",
    "main/src/ota_client.cpp",
    "main/src/ui_sdl.cpp",
    "main/src/ws_client.cpp",
    "main/src/main_sim.cpp",
]

try:
    sim_env.ParseConfig("pkg-config sdl2 --cflags --libs")
    sim_target = sim_env.Program(target=os.path.join(build_dir, sim_name), source=sim_sources)
    Default(app_target, sim_target)
except Exception:
    print("warning: SDL2 not found via pkg-config, simulator target disabled")
    Default(app_target)
