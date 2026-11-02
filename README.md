# VNEngine: A Lightweight C++ Visual Novel Engine

![C++](https://img.shields.io/badge/C++-17/20-blue.svg)
![SDL3](https://img.shields.io/badge/SDL-3.0-green.svg)
![Lua](https://img.shields.io/badge/Lua-Scripting-purple.svg)

**VNEngine** is a personal passion project—a lightweight, high-performance Visual Novel and 2D Adventure Game engine built from scratch using **C++** and **SDL3**. It features a robust Lua-based coroutine scripting system, asynchronous asset management, and a custom animation tweening pipeline.

This repository serves as a portfolio showcase of my self-taught journey in system-level programming, game engine architecture, and modern **AI-augmented software engineering workflows**.

## 🚀 Project Overview & Background

* **Status:** Personal Interest / Portfolio Project
* **Core Philosophy:** Exploring low-level graphics, memory management, and modern C++ development by building an engine from the ground up.
* **Development Approach:** Heavily developed with **Strong AI Assistance** (using tools like Cursor and LLMs), serving as an exploration of how modern developers can achieve high productivity while acting as the system architect.

## ✨ Core Features

* **Lua Coroutine Scripting (`LuaScriptContext`)**: Gameplay logic, dialogue sequencing, and scene transitions are strictly decoupled from the C++ core using Sol2/Lua. Supports complex suspension points (waits, choices, observe modes) with automatic state restoration.
* **Asynchronous Asset Management (`AssetManager`)**: Features background thread image decoding to prevent main-thread stuttering during heavy scene transitions, coupled with LRU-style cache management.
* **Action & Tweening Pipeline (`ActionQueue`)**: A custom, frame-driven scheduler for time-based visual effects (screen shakes, camera movements, sprite tinting, and letterboxing) independent of the immediate UI logic.
* **Advanced Rendering (`RenderPipeline`)**: Handles Z-indexed rendering, multiple background transition effects (crossfade, dissolve, blinds), and dynamic frosted-glass UI backdrops.
* **Robust Save/Load System (`SaveManager`)**: Serializes full engine state, Lua history logs, and variable maps into structured, tamper-resistant `.sav` files with automatic backup recovery.

## 🛠️ Tech Stack

* **Core Language:** C++ (C++17 / C++20)
* **Graphics & Audio Core:** SDL3, SDL3_image, SDL3_ttf, SDL3_mixer
* **Scripting:** Lua + Sol2

## 🤖 Development Philosophy: Strong AI-Assisted Engineering

I am fully transparent about the creation of this project: **this engine was built with heavy, intensive AI assistance.** 

Rather than hiding this, I view it as a demonstration of modern software engineering capability:
* **The Human Role (Architecture & Direction):** I acted as the system architect and lead designer. I defined the macro-level structure, designed the separation between C++ core systems and Lua scripting, established the memory safety patterns (smart pointers, mutex locks), and directed the debugging process.
* **The AI Role (Implementation & Acceleration):** AI tools were heavily utilized to write boilerplate code, accelerate the implementation of complex algorithms (such as the UTF-8 text wrapping and rich-text layout logic in `DialogueBox`), and rapidly adapt to the newly released SDL3 API.

This project showcases my ability to leverage cutting-edge AI tools to design, prototype, and ship a complex, functional systems-level application far beyond traditional solo development limits.

## 📂 Project Structure

* `src/Engine.h` & `main.cpp`: Main application loop, state management, and global lifecycle.
* `src/SceneManager.*` & `RenderPipeline.h`: Scene graph state, camera transforms, and Z-ordered rendering queue.
* `src/AssetManager.*`: Multithreaded texture preloading and font/audio resource caching.
* `src/LuaScriptContext.h` & `LuaRuntimeSafety.*`: Lua coroutine binding, instruction budget guards, and replay state tracking.
* `src/DialogueBox.*`, `ChoiceMenu.h`, `LogScreen.h`: Custom UI components with tween animations and rich-text support.
* `src/SaveManager.*` & `SaveSystem.*`: Data-only sandboxed Lua save serialization and snapshot restoration.
