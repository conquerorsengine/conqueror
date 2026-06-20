# Contributing to Conqueror's Engine

Thank you for your interest in contributing to Conqueror's Engine! This document provides guidelines and instructions for contributing.

## Code of Conduct

This project adheres to the Contributor Covenant Code of Conduct. By participating, you are expected to uphold this code. Please report unacceptable behavior to fazli@vertexishere.com.

## How to Contribute

### Reporting Bugs

- Use the GitHub issue tracker
- Describe the bug clearly
- Include steps to reproduce
- Include your environment details (OS, compiler version, etc.)

### Suggesting Features

- Open an issue with the "enhancement" label
- Describe the feature and its use case
- Explain why it would be useful

### Pull Requests

1. Fork the repository
2. Create a feature branch from `main`
3. Make your changes
4. Test thoroughly
5. Submit a pull request

### Development Setup

1. Clone the repository
2. Ensure you have CMake 3.20+ and a C++17 compiler
3. External dependencies are included in the `External/` directory
4. Build using CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build -j$(nproc)
```

### Code Style

- Follow the existing code conventions
- Use PascalCase for class names and public methods
- Use camelCase for local variables
- Use `m_` prefix for member variables
- Use `#pragma once` for header guards
- Include appropriate comments for complex logic

### Commit Messages

- Use descriptive commit messages
- Start with a verb in imperative mood (e.g., "Add", "Fix", "Update")
- Keep the subject line under 72 characters

## License

By contributing to Conqueror's Engine, you agree that your contributions will be licensed under the Mozilla Public License 2.0 with the additional clause that modifications to the game engine require source code disclosure.

Third-party libraries in the `External/` directory retain their original licenses.
