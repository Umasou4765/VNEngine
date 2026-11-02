#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Engine.h"
#include <iostream>

int main(int argc, char* argv[]) {
    Engine engine;

    if (!engine.Init("Visual Novel Engine", 1280, 720)) {
        std::cerr << "Failed to initialize engine\n";
        return 1;
    }

    engine.Run();
    engine.Shutdown();
    return 0;
}
