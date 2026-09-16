[AI_INSTRUCTION_SET:CPP11_INTELLIJ_ASSISTANT]  
version=1.0  
scope=c++11,defensive_code,no_circular_deps

// CORE DIRECTIVES
1. **C++11 Syntax Enforcement**
    - Trigger: Detect any code using post-C++11 keywords/features (e.g., `constexpr if`, `auto` return types).
    - Action:
        * Flag with "C++11 Violation: [Feature] requires C++[X]."
        * Suggest alternative (e.g., replace `make_unique` with `unique_ptr<T>(new T(...))`).
        * Insert `//NOLINT` override only if user confirms.

2. **Header Guard Protocol**
    - When: New header file created/modified.
    - Verify:
        * `#pragma once` present
        * No nested `#include` cycles (use IntelliJ's "Diagram > Show Dependencies")
    - Fix: Auto-insert forward declarations instead of full includes where possible.

3. **Null Safety Routines**
    - Pre-check all pointer dereferences:
        * If raw pointer declared without immediate `nullptr` init:
          > "Uninitialized pointer risk. Apply:  
          [1] Add `= nullptr`  
          [2] Convert to `std::shared_ptr` (C++11)"
        * Before `->` operator: Enforce explicit null check pattern:  
          `if (!ptr || !ptr->isValid()) { /* handle */ } else { ptr->op(); }`

4. **Circular Dependency Prevention**
    - Detect chain: If ClassA.h includes ClassB.h which includes ClassA.h.
    - Response:
        * "Circular dependency detected. Refactor steps:  
          [Quick Fix] Replace #include with forward decl in ClassA.h  
          [Refactor] Extract interface to ClassAInterface.h"
    - Auto-suggest PIMPL idiom when classes exceed 3 member objects.

// INTEGRATION WITH INTELLIJ FEATURES
5. **Toolchain Sync**
    - Monitor `CMakeLists.txt`/`.idea/misc.xml` for:
        * `-std=c++11` flag presence
        * `-Werror=null-dereference` compiler args
    - If missing: Create inspection warning with "Add C++11 compliance flags?" quickfix.

6. **Live Template Expansion**  
   Replace patterns:
    - User types `forptr` > Expand to:  
      `for (auto* item : collection) { if (!item) continue; ... }`
    - User types `safeaccess` > Expand to:  
      `if (auto locked = weak.lock()) { locked->call(); }`

// DEFENSIVE CODING CHECKS
7. **Exception Flow Control**
    - If `throw` detected in codebase with `-fno-exceptions`:
        * Hard error: "C++ exceptions disabled. Use error codes or `std::optional`."
        * Suggest alternative:  
          `std::optional<Result> tryOp() { ... }`

8. **Resource Ownership**
    - On `new` keyword:
        * Immediate warning: "Manual memory allocation detected."
        * Quick actions:  
          [1] "Wrap with std::unique_ptr"  
          [2] "Add deletion guarantee via RAII"

// RESPONSE PROTOCOLS
9. **User Query Handling**
    - If asked "How to make this dynamic?":
        * Respond with "Dynamic allocation prohibited per guidelines. Workaround:"
        * Provide fixed-capacity pattern:  
          `std::array<Led, MAX_LEDS> leds; // Use index guards`

10. **Ambiguity Resolution**
    - When multiple solutions exist:
        * Prioritize:
            1. Solutions using IntelliJ's built-in refactoring tools
            2. C++11-only patterns
            3. Minimal memory overhead approaches


/*

* ---------------------------------------------------------------------------
*  Project Guidelines and Instructions
* ---------------------------------------------------------------------------
*  Goal:
*  - Develop a real-time LED lighting system for an ESP32 microcontroller.
*  - The system uses an INMP441 I2S microphone to analyze audio input and
*    dynamically control LED animations and layers based on audio features
*    and mood detection.
*  - The project is compiled using PlatformIO with the Arduino framework.
*
* ---------------------------------------------------------------------------
*  Hardware:
*  - Microcontroller: ESP32
*  - Microphone: INMP441 (I2S interface)
*  - LED Strips: WS2812B (or similar) controlled via FastLED library
*
* ---------------------------------------------------------------------------
*  Hard Requirements:
*  1. **C++11 Compatibility**:
*     - The ESP32 Arduino Core (used in Arduino IDE/PlatformIO) compiles with C++11.
*     - Do not use features from newer C++ standards (e.g., C++14, C++17, etc.).
*
*  2. **Compile-Time LED Configuration**:
*     - All LED strip configurations (e.g., pin numbers, number of LEDs) must be 
*       defined as compile-time constants.
*     - Use `#define` or `constexpr` for LED configuration.
*     - Avoid runtime reconfiguration of LED strips.
*
*  3. **No Dynamic Reconfiguration of Strips**:
*     - LED strips must be statically defined and initialized during setup.
*     - Avoid adding or removing LED strips dynamically at runtime.
*
* ---------------------------------------------------------------------------
*  Software Design:
*  - Use the FastLED library for LED control.
*  - Use the I2S interface to capture audio data from the INMP441 microphone.
*  - Implement audio feature extraction (e.g., energy, beat detection, etc.).
*  - Use extracted audio features to control LED animations and layers.
*  - Organize code into modular components:
*      - Audio input and processing
*      - LED strip management
*      - Scene and animation control
*      - Mood detection and history tracking
*
* ---------------------------------------------------------------------------
*  Constraints:
*  - Minimize dynamic memory allocation (e.g., avoid `new`/`delete`).
*  - Ensure real-time performance for smooth LED animations.
*  - Use lightweight data structures and algorithms to fit within ESP32's
*    memory and processing constraints.
*
* ---------------------------------------------------------------------------
*  Example LED Configuration:
*  - Define LED strips as compile-time constants:
*      #define LED_0_PIN 5
*      #define LED_0_NUM 60
*      #define LED_1_PIN 18
*      #define LED_1_NUM 120
*  - Initialize LED strips in the `begin()` method using `FastLED.addLeds`.
*
* ---------------------------------------------------------------------------
*  Notes:
*  - Follow the above guidelines strictly to ensure compatibility and
*    performance on the ESP32 platform.
*  - Test the system thoroughly on the intended hardware.
* ---------------------------------------------------------------------------
*/