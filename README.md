# JSONTitan

JSONTitan is a high-performance, C++ Qt6-based desktop application engineered to open, navigate, and analyze extremely large JSON files without compromising system responsiveness. 

Designed for developers, data scientists, and engineers who routinely work with massive datasets, JSONTitan provides intuitive tree-based navigation, powerful search capabilities, multi-file unioning, and versatile export options.

## ✨ Features

* **High-Performance Large File Support:** Optimized memory management and parsing to open gigabyte-sized JSON files that crash standard text editors.
* **Hierarchical Tree View:** Navigate complex JSON structures effortlessly. The UI parses and displays nested objects and arrays in a clean, collapsible tree view.
* **Advanced Search & Filtering:** A lightning-fast search bar allows you to filter keys and values across the entire document, instantly highlighting and isolating relevant sub-objects.
* **Multi-File Union:** Open and combine multiple JSON files into a single, unified workspace. This allows you to view, search, and analyze data across disparate files concurrently within the same window.
* **Versatile Exporting:** Seamlessly convert and export your parsed JSON data (or filtered subsets) into **CSV** or **XML** formats for use in external spreadsheet tools or legacy systems.

## 🏛️ Architecture

This project strictly adheres to the **Functional Core, Imperative Shell** architectural pattern to ensure high testability, maintainability, and thread safety.

* **Functional Core:** All data transformations, JSON parsing, tree-building logic, search filtering, and formatting operations (CSV/XML generation) exist within a pure, functional core. 
  * **I/O Constraint:** State mutation and side-effects are strictly prohibited in the core. The *only* Input/Output (I/O) operation permitted within the core is appending to diagnostic log files.
* **Imperative Shell:** The Qt6 graphical user interface, file system interactions, thread management, and memory allocations act as the imperative shell. The shell is responsible for gathering user input, feeding immutable data structures into the functional core, and displaying the resulting pure outputs back to the user.

## 🛠️ Prerequisites

To build and run this project, you will need the following installed on your system:

* **C++ Compiler:** Supporting C++23
* **CMake:** Version 3.16 or newer.
* **Qt6:** The core framework (Specifically `Core`, `Gui`, and `Widgets` modules). 

## 🚀 Building from Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/JSONTitan.git
   cd JSONTitan
   ```

2. **Create a build directory:**
   ```bash
   mkdir build && cd build
   ```

3. **Run CMake and build:**
   ```bash
   cmake ..
   cmake --build .
   ```

4. **Run the application:**
   ```bash
   ./JSONTitan  # Or the respective executable name on your OS
   ```

## 📖 Usage Guide

* **Opening Files:** Use `File > Open` to load a single JSON file. To utilize the Union feature, use `File > Union Files` and select multiple JSON documents; they will populate under a single root node in the tree view.
* **Navigating:** Expand and collapse nodes in the left-hand tree view to explore data arrays and nested objects.
* **Searching:** Type into the top search bar. The tree view will automatically prune to show only the nodes containing the matched keys or values.
* **Exporting:** Select a specific node (or the root node), right-click, and choose `Export to CSV` or `Export to XML`.

## 🤝 Contributing

Contributions, issues, and feature requests are welcome! 

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

Distributed under the MIT License. See `LICENSE` for more information.