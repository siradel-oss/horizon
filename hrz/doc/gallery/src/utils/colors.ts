export type ColorName =
    | "onPrimary"
    | "onPrimaryContainer"
    | "onSecondary"
    | "onSecondaryContainer"
    | "onSurface"
    | "onSurfaceVariant";

// We have to write those classes without any string interpolation of things like
// that so that Tailwind can detect them and add them to the final CSS file.

export const COLOR_CLASS: Record<ColorName, { text: string; bg: string }> = {
    onPrimary: { text: "text-onPrimary", bg: "bg-onPrimary" },
    onPrimaryContainer: { text: "text-onPrimaryContainer", bg: "bg-onPrimaryContainer" },
    onSecondary: { text: "text-onSecondary", bg: "bg-onSecondary" },
    onSecondaryContainer: { text: "text-onSecondaryContainer", bg: "bg-onSecondaryContainer" },
    onSurface: { text: "text-onSurface", bg: "bg-onSurface" },
    onSurfaceVariant: { text: "text-onSurfaceVariant", bg: "bg-onSurfaceVariant" },
};
