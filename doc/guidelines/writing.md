+++
title = "Writing guidelines"
+++

# Writing guidelines

Don't use unnecessary capitalization.

> [!success]
> Added independent cameras in the multiview system.

> [!failure]
> Added the Multiview system.

---

In code use US English, but in text use British English.

---

Use "they" instead of "he" or "she" when referring to a person.

---

Use "it" instead of "he" or "she" to refer to a non-person.

> [!success]
> Copilot implemented the feature. He did it quick!

> [!failure]
> Copilot implemented the feature. It did it quick!

---

When documenting a method, phrase the description in terms of what the method does, not what the developer would use it to do.

> [!success]
> * Creates a layer
> * Adds an element, then returns the total number of elements.

> [!failure]
> * Create a layer.
> * Add an element. Get the total number of elements back.

---

Refer to existing API types and services by linking them to `$proto`, except where it makes the sentence too hard to read.

> [!success]
> * `[ImageFormat]($proto)`
> * `A [Bing provider](/doc/reference/HrzProtocol.BingProviderRasterParams) can be configured to (...)`

> [!failure]
> `[ImageFormat](/doc/reference/HrzProtocol.ImageFormat)`
