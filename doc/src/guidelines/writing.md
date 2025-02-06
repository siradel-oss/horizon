---
title: Writing
---

# Writing guidelines

Don't use unnecessary capitalization.

!!! success ""
    Added independent cameras in the multiview system.

!!! failure ""
    Added the Multiview system.

---

In code use US-style words.

!!! quote ":flag_us: Freedom words"
    Color, behavior, neighbor, ...

In text (documentation, changelog, ...), use british-style words.

!!! quote ":flag_gb: Distinguished words"
    Colour, behaviour, neighbour, ...

---

Use "they" instead of "he" or "she" when referring to a person.

---

Use "it" instead of "he" or "she" to refer to a non-person.

!!! success ""
    Copilot is not smart. It just extrapolates language.

!!! failure ""
    Copilot is not smart. He just interpolates language.

---

When documenting a method, phrase the description in terms of what the method does, not what the developer would use it to do.

!!! success ""
    * Creates a layer
    * Adds an element, then returns the total number of elements.

!!! failure ""
    * Create a layer.
    * Add an element. Get the total number of elements back.

---

Refer to existing API types and services by surrounding them with `[[` and `]]`, except where it makes reading more difficult.

!!! success ""
    * `[[ImageFormat]]`
    * `A [Bing provider](HrzProtocol.BingProviderRasterParams.html) can be configured to (...)`

!!! failure ""
    `[ImageFormat](HrzProtocol.ImageFormat.html)`
