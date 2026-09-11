// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

import { HrzProtocol } from "@siradel-oss/horizon-protocol";

export type ColorName =
    | "onPrimary"
    | "onPrimaryContainer"
    | "onSecondary"
    | "onSecondaryContainer"
    | "onSurface"
    | "onSurfaceVariant"
    | "onSecondaryFixedVariant";

// We have to write those classes without any string interpolation of things like
// that so that Tailwind can detect them and add them to the final CSS file.

export const COLOR_CLASS: Record<ColorName, { text: string; bg: string }> = {
    onPrimary: { text: "text-onPrimary", bg: "bg-onPrimary" },
    onPrimaryContainer: { text: "text-onPrimaryContainer", bg: "bg-onPrimaryContainer" },
    onSecondary: { text: "text-onSecondary", bg: "bg-onSecondary" },
    onSecondaryContainer: { text: "text-onSecondaryContainer", bg: "bg-onSecondaryContainer" },
    onSurface: { text: "text-onSurface", bg: "bg-onSurface" },
    onSurfaceVariant: { text: "text-onSurfaceVariant", bg: "bg-onSurfaceVariant" },
    onSecondaryFixedVariant: {
        text: "text-onSecondaryFixedVariant",
        bg: "bg-onSecondaryFixedVariant",
    },
};

export function colorFromHex(hex: string): HrzProtocol.Color {
    if (hex.startsWith("#")) {
        hex = hex.slice(1);
    }
    if (hex.length !== 6 && hex.length !== 8) {
        throw new Error(`Invalid hex color: ${hex}`);
    }
    const clamp = (value: number) => Math.max(0, Math.min(1, value));
    const r = clamp(parseInt(hex.slice(0, 2), 16) / 255);
    const g = clamp(parseInt(hex.slice(2, 4), 16) / 255);
    const b = clamp(parseInt(hex.slice(4, 6), 16) / 255);
    const a = hex.length === 8 ? clamp(parseInt(hex.slice(6, 8), 16) / 255) : 1;
    return HrzProtocol.Color.create({ r, g, b, a });
}
