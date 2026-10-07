# SalerPromts

A high-performance cross-platform desktop application built with **Qt 6** and **Modern C++** designed for deterministic, privacy-first offline prompt engineering and multi-agent AI workflow automation. 

The system operates entirely on-device, leveraging local **llama.cpp** server infrastructures to orchestrate GGUF models without cloud dependencies, ensuring complete corporate data compliance.

---

##
🛠 Core Technical Features & Architecture

    On-Device AI Engine Integration: Direct orchestration and state management of local llama-server processes. Handles prompt engineering iterations asynchronously directly against local GGUF models.
    Smart Resource Management: Native C++ optimization layers designed to control inference parameters, session contexts, and hardware constraints safely.
    Fluid Modern UI: Reactive desktop user interface engineered with Qt 6, optimizing memory usage and ensuring smooth thread separation between the C++ inference engine and the GUI rendering cycle (supporting MSVC / Clang / GCC).
    Deterministic Local Storage: Utilizes local JSON catalogs and structured databases (data/products.json, data/results.json) for zero-latency storage of prompt matrices and model outputs.
    Asynchronous Execution: Heavy server IO operations and model interactions are completely decoupled from the main GUI thread to maintain maximum desktop responsiveness.

---

## 💻 Tech Stack & Requirements

* **Framework:** Qt 6.x (Successfully compiled on **Qt 6.11.1**)
* **Compiler:** MSVC 2022 (64-bit) / Modern C++ Standards
* **AI Core Backend:** `llama.cpp` (Local server architecture)
* **Model Format:** GGUF (Optimized for local CPU/GPU layer split)
* **Data Serialization:** Modern JSON pipelines

---

## 📂 Project Structure & Catalogs

* `src/` — Primary C++ backends, custom Qt components, and application logic.
* `data/products.json` — Structured local catalog managing local contexts and entities.
* `data/results.json` — Local database storage tracking deterministic model outputs and prompt histories.

---

## 🚀 Strategic Architecture Note

This project serves as a showcase of bridging performance-critical **C++ local ML backend inference** with scalable **Qt desktop presentation layers**. Engineered under strict privacy-first constraints, it highlights extreme footprint minimization, local process orchestration, and advanced prompt-engineering automation tools.
