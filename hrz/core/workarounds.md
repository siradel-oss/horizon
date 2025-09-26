Workarounds are marked with the `@Workaround(ID)` annotation in the code.

# Active

## `001-Firefox-DrawElementsWithNoAttributes` (2022-12-01)

In Firefox < 107 (released on 2022-11-15), calling glDrawElements without any active vertex attributes *per vertex* does not work. So we have to make a dummy vertex attribute and use it in the shader.

## `002-AngleWindows-StructuredBufferNameAndLoading` (2022-12-13)

Angle on Windows (when using D3D as backend) sometimes translated constant buffers to structured buffers for compilation efficiency reasons. However, there are issues where uniform blocks with an alias get their alias lost so we need to use the name of variables inside the block instead.

Additionally, when the large array contained in the block it not of a primitive type (matrices, vectors, etc.) there is a translation step that needs to happen. The way it is done is by loading all the structures in registers. Of course this is stupid and greatly hurts performances and compile times (which this technique was supposed to avoid). It is crucial to avoid reading from these translated structures by loading them from uniform memory field by field:

```glsl
struct MyStruct
{
    float a;
    float b;
};

layout(std140) uniform MyBlock
{
    MyStruct my_structs[32];
}; // Note: No alias.

void main()
{
    // Very good, but very bad! Don't to this!
    MyStruct s = my_struct[gl_InstanceID];

    // Very bad but unfortunately necessary.
    MyStruct s;
    s.a = my_struct[gl_InstanceID].a;
    s.b = my_struct[gl_InstanceID].b;
}
```

References

- https://chromium.googlesource.com/angle/angle/+/refs/heads/main/src/libANGLE/renderer/d3d/d3d11/UniformBlockToStructuredBufferTranslation.md
- https://bugs.chromium.org/p/angleproject/issues/detail?id=3682

## `003-Firefox-FragmentOutputFragDepth` (2022-12-15)

In Firefox, having any kind of code that writes to `gl_FragDepth` before (in a textual sense, not related to execution) the definition of a fragment output might change the type of said output. This means that we must be very careful about the order of code.

```glsl
// Bad: the type of my_output will be incorrectly recognized as vec4
void write_depth() { gl_FragDepth = 0.5; }
layout(location = 0) out uvec4 my_output;

// But this will work as expected.
layout(location = 0) out uvec4 my_output;
void write_depth() { gl_FragDepth = 0.5; }
```

## `004-Safari-UniformBufferArrayLoad` (2023-01-04)

In Safari, loading data from an array inside a uniform buffer can sometimes be very slow. Some workarounds for this include putting the whole array in registers, or indexing by a constant and using some other branching mechanism to select the correct output.

## `005-Android-LoadDataToStructure` (2023-01-10)

For some reason, using data from a structure sometimes fails in GLSL on Android. So avoid copying data to a structure and then using it. Just copy the fields to vector registers.

Sometimes this manifests as an assertion fail during shader compilation.

## `006-Android-LowFloatPrecisionInStructs` (2023-01-17)

On a lot of Android devices using a Qualcomm GPU, the precision of floats in lowered when they are read from a struct. More info there:
 - https://github.com/KhronosGroup/WebGL/issues/3351
 - https://gkjohnson.github.io/webgl-precision/

## `007-Apple-ArithmeticPrecisionLoss` (2023-03-30)

When computing the difference between two double-precision floating-point numbers that have each been split into two single-precision `float`s, macOS and iOS (apparently) merge successive arithmetic operations in a way that degrades the precision of the end result.

Simply storing intermediate steps of the computation in their own variables does not suffice, nor adding `highp` precision qualifiers. To force the GPU to compute intermediate values, dummy `if` branches – that cannot be taken – are used. Evaluating their conditions forces the need for the intermediate values, which can then be added together safely.

## `008-Apple-ParallelShaderCompile` (2023-05-25)

Using the parallel shader compile WebGL extension on iOS or MacOS creates long freezes. So we don't. Yay.

## `009-Apple-SoAUboCompileTime` (2023-05-25)

It seems that large AoS UBO make compile time very high on Apple machines. Use SoA instead if this happens.

## `011-Chromium-RangeRequests-Cache` (2024-11-14)

When doing lots of range requests to the same resource in Chromium-based browsers, sometimes their caching thing fails and we get no response. Adding a different query parameter for each request seems to fix this.

See https://issues.chromium.org/issues/40542704.

Also Firefox has trouble with this: https://bugzilla.mozilla.org/show_bug.cgi?id=1615698.

## `012-Emscripten-ModuleLeakOnExit` (2025-01-16)

Because of an `Atomics.waitAsync` promise in Emscripten’s `__emscripten_thread_mailbox_await`, that remains alive after the Web Assembly program has exited, and retains the whole module, its memory is never released. This includes the shared array buffer.

Waking up the promise and making `checkMailbox` (the function that calls `__emscripten_thread_mailbox_await`) empty allows getting rid of all references to the module.

The workaround is based on the one in this comment: https://github.com/emscripten-core/emscripten/issues/20920#issuecomment-2565983153

# Retired

## `010-Chromium-Emscripten-TextDecoder` (2024-10-22)

* HRZ-1081
* https://github.com/emscripten-core/emscripten/issues/15217
* https://github.com/emscripten-core/emscripten/issues/18034
* https://github.com/emscripten-core/emscripten/pull/16994
