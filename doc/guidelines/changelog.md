+++
title = "Changelog guidelines"
+++

# Changelog guidelines

> [!note]
> [Writing guidelines](writing.md) also apply to changelogs.

---

The possible changelog sections are:

* **Added**: New features and capabilities.
* **Changed**: Features whose behaviour has been modified, or improvements that don't bring new capabilities.
* **Deprecated**: Things that will be removed in a future update.
* **Removed**: Things that have been removed in this update.
* **Fixed**: Anomalous behaviours that have been rectified.
* **Upgrade notes**: Actions necessary by integrators to keep their application working.
* **Integration notes**: Information or guidelines to help integrators integrate Horizon optimally.

---

In general, use the active voice in the past tense.

> [!success]
> * Added a system to highlight features on mouse hover.
> * Fixed a crash when receiving GeoJson features with empty geometry.
> * Renamed the `SILICIUM_F32` image format to `SIRADEL_F32`.

> [!failure]
> * Adds a system to highlight features on mouse hover.
> * A system to highlight features on mouse hover has been added.
> * New feature highlight system.

---

In the "Changed" section, where forming such a sentence can be tricky, bullet points can be written in terms of what has changed instead.

> [!success]
> * Changed the default resolution of vector tiles from 3 to 5.
> * Flat polylines are now anti-aliased.

> [!failure] Formulation is not great
> * Made flat polylines anti-aliased.

---

Under such a bullet point, in a sub-list, more information can be given in any form. Try to keep sentences short and to the point.

> [!success]
> * Added a system to highlight features on mouse hover.
>     * Its update frequency can be configured.
>     * This system is more efficient than the selection system.

> [!failure] Should be grouped
> * Added a system to highlight features on mouse hover. Its update frequency can be configured.
> * This system is more efficient than the selection system.

---

When multiple bullet points refer to the same system or use case, they can be reorganized under a common category. This category should be written in **bold** and be very short.

> [!success]
> * **Mouse hover interactions**
>     * Added a system to highlight features on mouse hover.
>     * Added a configuration option for the update rate of mouse hover information callback.

> [!failure] Should be grouped
> * Added a system to highlight features on mouse hover.
> * Added a configuration option for the update rate of mouse hover information callback.

> [!failure] Title is too long
> * **The mouse hover highlighting & info system**
>     * Added a system to highlight features on mouse hover.
>     * Added a configuration option for the update rate of mouse hover information callback.

---

Phrase the bug fixes in terms of what a system was doing wrong. When possible, try to be specific about what the issue was.

> [!success]
> * Fixed impostors looking too dark when lighting was enabled.

> [!failure]
> * Fixed impostors sometimes looking too dark.
> * The impostors were looking too dark when lighting was enabled.
> * The lighting of impostors has been fixed.
> * Impostors now appear bright enough.

---

In upgrade and integration notes, the meaning of the terms `must`, `should`, and `may`/`can` is defined by [RFC 2119](https://datatracker.ietf.org/doc/html/rfc2119).

> [!success]
> * The `url` field of `VectorDataSource` **must** be renamed to `urlPattern`.
>     * This means that the system won't work anymore if it's not done.
> * The new `opacity` field of rasters **should** be set to 1.
>     * This means that the best default value to match the behaviour of the previous version is 1, however the integrator may choose to use a different value if it is more appropriate for their use case.

---

Optional changes should be left to integration notes, instead of upgrade notes. Upgrade notes and integration notes can follow a looser writing style than the other sections.
