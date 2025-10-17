---
title: Development
---

# Development guidelines

Not everything is formally specified. Generally, try to follow the style surrounding whatever you are working on. Diverging can be OK sometimes. Some rules also apply from the [writing guidelines](writing.md).

## Code formatting

All code should be formatted using the `tools/format.py` script. It is checked during merge requests. It is possible to only format staged files using the `--staged` options.

!!! warning "Manually formatting code"
    Some code is not formatted (Python, Bazel, and other things). In this case it is up to you to format it as you go to respect what is around it. There are no hard rules, but be mindful of how you write this code.

The general guidelines are:

- **Indenting**: 4 spaces, Allman for C++, K&R 1TBS for TS & JS.
- **End of lines**: LF
- **Encoding**: UTF-8 w/o BOM
- **File names**: snake_case.
- **Line width**: Try to limit to 80, don't go over 120.
- Don't be too clever about alignment.
- Try to include files in a logical way.

## Naming

Name things clearly, but try do not be too lengthy either.

| | C++ | TS/JS |
|-|-----|-------|
| Types | PascalCase | PascalCase |
| Variables | snake_case | camelCase |
| Functions | snake_case | camelCase |
| Enums values | PascalCase<br />kPascalCase<br />EnumName_PascalCase (when scoping matters) | FULL_CAPS |
| Constants | FULL_CAPS<br />PascalCase<br />kPascalCase | FULL_CAPS |
| Namespace | snake_case | PascalCase |

As you case see, in C++, there are multiple options for enums and constants. Try to use whatever style is used in the code around whatever you are writing, or use your best judgement. For example:

- `EnumName_PascalCase` can be good for flags that use non-class enums, but is verbose.
- `PascalCase` is shorted, be can lead to name conflicts, and be mixes up with a type name.
- `kPascalCase` can make the fact that the symbol is a constant clearer, but is a newer, less used convention.
- `FULL_CAPS` can be used to convey that this is a symbolic constant whose value matters, but is a bit obnoxious.

Prefixes can be used for particular semantics:

- `_`: private things, or member variables with invariants that should not be used directly.
- `g_`: global mutable variables.

## Engineering

### The language is not the platform

All code runs on a computer. The computer is the platform. This means that the language is just the interface to the machine. In practice, any utility or abstraction provided by the language should be evaluated in terms of what it actually does, not just how many lines of code it saves or how "elegant" the resulting code is. Some costs that should be evaluated but are commonly ignored by the mainstream programming mindset are:

* **What is the cognitive load of using a feature or primitive?**
  How much background knowledge do I have to acquire to understand the primitive and what it does both on a fundamental and practical level.
  This is important to reduce the cost of onboarding and maintenance.
* **How much compile time will be lost on a feature?**
  For example, C++ templates can be amazing, but designing an entire application to use static polymorphism everywhere means everything physically depends on everything else. Compile times for large systems designed like this can easily balloon and make the edit-compile-run cycle take more than a minute.
  Higher compile times mean higher iteration times. And developers with higher iteration times are generally unhappier, and unwilling to go back and fix small issues if that costs 10 minutes of compile time.

!!! abstract "TL;DR"
    Evaluate language feature for maintenance and compile time costs before using them.

### Design around data flow & layout first

Almost all problems we solve can be described as what data comes in, and what data comes out. The transformation in the middle is usually a direct consequence of those two parameters. Usually this leads to a simple and testable design.

These days one of the most important performance opportunity is not pessimizing the layout of the data. Basically designing the data layout around locality and access patterns. For example, some parameters to think about are: will the data be read in tight loops, or once every second? Same for writes? Will I get more writes or reads? Will the accesses always be random or sequential?...

Tangentially, thinking carefully about memory allocations can be an easy win for performance: allocating in big groups instead of lots of small objects, reusing allocated memory in tight loops instead of deallocating and reusing constantly, preallocating arrays, memcpy-ing trivial data, etc.

!!! abstract "TL;DR"
    Think about how data will be accessed before designing how it is stored and accessed. Reduce the number of memory allocations.

### Comments, and when (not) to use them

Comments that describe the code are frowned upon. This includes "documentation comments": in those cases the code or the interface to the code should be self describing, as much as possible. Some exceptions include describing non-trivial preconditions, ownership of parameters or return values, etc. Generally, if a comment could have been generated by an LLM, it should be omitted. The reason for this is that it incurs a maintenance cost while not providing any information.

However comments that describe how a system works or why some code exists is extremely important and should be written and read with the upmost care. For example, a large paragraph at the top of a system's implementation explaining its design and the reasons for it is extremely useful for maintenance. Also, comments explaining that some code has been written in a particular fashion because of performance considerations, or because of a workaround, will help future developers not overlook potential issues. Some of these comments can be signed and dated if they need referencing.

Also, keep in mind that what we do is public. So no profanities or unprofessional things in comments.

!!! abstract "TL;DR"
    Use comments to explain systems and design considerations, not to describe what is obvious.

### Keep public interfaces minimal

This is a bit specific to C++, but generally only the public interface of a system should be exposed. It is particularly important in C++ because of compile times.

A common way of writing C++ consists in writing one class per system or use case, and use `public` and `private` to separate the public interface from the implementation. This is hugely wasteful in C++ because the compilation model makes it mandatory to recompile the public API consumers even if you have only changed implementation details in the header file. Some systems like precompiled headers exist, but they generally are platform dependent, or not very usable.

Also it is generally more useful to be able to read a file containing only what can be used from the outside instead of having to remember if a particular method or field was in a public or private section.

Here are some techniques for dealing with this.

Exposing an interface and a constructor for the implementation.

```cpp
class IMySystem
{
public:
    virtual void do_something() = 0;

    // The returned system is owned by the caller.
    static IMySystem* create();
    static void destroy(IMySystem*);
};
```

Using forward-declared types (Most commonly used in Horizon).

```cpp
class MySystem;

MySystem* create();
void destroy(MySystem*);
void do_something(MySystem*);
```

Note that here we don't use RAII for those systems, this is related to the next part.

!!! abstract "TL;DR"
    At a large scale, do not expose any implementation details. Hide them behind interfaces or forward-declared types.

### Inject dependencies at call sites, not at construction

Common strategies to access required systems are using global accessors (like singletons), or injecting those dependencies when constructing a system.

```cpp
// DON'T DO THIS
class AssetsLoader;
class ModelLoader;
ModelLoader* create(AssetsLoader*); // AssetsLoader retained in the ModelLoader
void destroy(ModelLoader*);
void load_model(ModelLoader*, const char* path);
```

The issue with this approach is that it can over time create a dense mesh of dependencies between systems, that can be hard to untangle, especially as related to systems lifetime and initialization order.

Instead, it is better to not retain references to other systems, and instead inject them from every call site where they will be needed. This makes dependencies explicit: if a function that draws a triangle needs the assets loader, we might have a design issue!

```cpp
class ModelLoader;
ModelLoader* create();
void destroy(ModelLoader*, AssetsLoader*);
void load_model(ModelLoader*, AssetsLoader*, const char* path);
```

There are still drawbacks to this approach: it can be very cumbersome, and there is no guarantee to receive the same instance of a system twice.

The first issue can be solved by grouping dependencies in context structures, or for global systems like a logger, using global references. The second issue is extremely rare, and is far outweighed by the other benefits.

!!! abstract "TL;DR"
    Pass dependencies at call sites, and don't retain references to them.

### Avoid boilerplate

Boilerplate code is a code smell. Use code generation or refactor to avoid it. Be mindful though, duplication is not necessarily boilerplate. Duplicating code is fine if is avoids creating a hugely complex system full of templates and weird parameters. Having to edit 28 files to just add a new field to the scene model is not. Just like comments, if some code could have been generated by an algorithm, it should be avoided.

Another example is getters and setters. Most of the time public fields are fine when there are no invariants.

!!! abstract "TL;DR"
    Boilerplate code increases maintenance costs. Factor it or generate it instead.

### Testing

Tests are extremely important but should not be ritualized. Test what is necessary, and use a testing methodology that does not require you to mock half the engine.

For example unit-testing that an URL parsing function reads the correct protocol is simple, but unit-testing that an animation is displayed at the correct rate is hard. In those cases, [visual tests](../tools/visual_tests.md) are more appropriate.

Also, do not test too much. Testing everything makes is very hard to change public interfaces because then all tests break. Choose your battles. Testing getters and setters is probably useless. See the section about boilerplate above.

!!! abstract "TL;DR"
    Only test what is necessary, and using appropriate methodologies. If a systems needs to be modified to be tested, the testing methodology is not appropriate.

### Writing C++

We use C++20, but not all of it. Generally if a language feature or library is never used, ask yourself and others why before introducing it in the code.

- Do not use `std::shared_ptr` to design around ownership issues.
- Strive for const-correctness.
- Prefer explicit structs to `std::pair` and `std::tuple`.
- No multiple inheritance (except for pure virtual classes).
- Keep classes hierarchies as shallow as possible, or just avoid them.
- Use `#pragma once`.
- Private field names start with an underscore.
- Prefer `std::string_view` to `const char*` and to `std::string`, where possible.
- Prefer `std::span` for passing contiguous sequences instead of arrays or vectors.
- Runtime strings are UTF-8.
- No exceptions.
- Prefer `hrz::flat_hash_(map|set)` to `std::unordered_(map|set)`. More info [here](https://abseil.io/docs/cpp/guides/container).
- Some C++ standard library features that are only included in later C++ versions are made available through `hrz_fnd` thanks to abseil.
