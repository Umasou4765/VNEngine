#pragma once

class Engine;

class SystemInitializer {
public:
    static bool InitAll(Engine& engine, const char* title, int w, int h);
};