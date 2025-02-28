import path from "path";

// Color generate from https://material-foundation.github.io/material-theme-builder/
// Using base color #3E5AFF

const BAZEL_PACKAGE = process.env.BAZEL_PACKAGE || __dirname;

/** @type {import("tailwindcss").Config} */
module.exports = {
  content: [
    path.resolve(BAZEL_PACKAGE, "index.html"),
    path.resolve(BAZEL_PACKAGE, "src/**/*.{vue,ts}"),
  ],
  darkMode: "class",
  theme: {
    fontFamily: {
      sans: ["Roboto", "sans-serif"],
      mono: ["monospace"],
    },
    extend: {
      fontSize: {
        mDisplayLarge: ["57px", {
          lineHeight: "64px",
          fontWeight: "400",
          letterSpacing: "-0.25px",
        }],
        mDisplayMedium: ["45px", {
          lineHeight: "52px",
          fontWeight: "400",
        }],
        mDisplaySmall: ["36px", {
          lineHeight: "44px",
          fontWeight: "400",
        }],
        mHeadlineLarge: ["32px", {
          lineHeight: "40px",
          fontWeight: "400",
        }],
        mHeadlineMedium: ["28px", {
          lineHeight: "36px",
          fontWeight: "400",
        }],
        mHeadlineSmall: ["24px", {
          lineHeight: "32px",
          fontWeight: "400",
        }],
        mTitleLarge: ["22px", {
          lineHeight: "28px",
          fontWeight: "400",
        }],
        mTitleMedium: ["16px", {
          lineHeight: "24px",
          fontWeight: "500",
        }],
        mTitleSmall: ["14px", {
          lineHeight: "20px",
          fontWeight: "500",
        }],
        mBodyLarge: ["16px", {
          lineHeight: "24px",
          fontWeight: "400",
          letterSpacing: "0.5px",
        }],
        mBodyMedium: ["14px", {
          lineHeight: "20px",
          fontWeight: "400",
          letterSpacing: "0.25px",
        }],
        mBodySmall: ["12px", {
          lineHeight: "16px",
          fontWeight: "400",
          letterSpacing: "0.4px",
        }],
        mLabelLarge: ["14px", {
          lineHeight: "20px",
          fontWeight: "500",
          letterSpacing: "0.1px",
        }],
        mLabelMedium: ["12px", {
          lineHeight: "16px",
          fontWeight: "500",
          letterSpacing: "0.5px",
        }],
        mLabelSmall: ["11px", {
          lineHeight: "16px",
          fontWeight: "500",
          letterSpacing: "0.5px",
        }],
      },
    },
    opacity: {
      "0": "0%",
      hover: "8%",
      active: "10%",
    },
    colors: {
      primary: "rgb(var(--color-primary) / <alpha-value>)",
      surfaceTint: "rgb(var(--color-surface-tint) / <alpha-value>)",
      onPrimary: "rgb(var(--color-on-primary) / <alpha-value>)",
      primaryContainer: "rgb(var(--color-primary-container) / <alpha-value>)",
      onPrimaryContainer: "rgb(var(--color-on-primary-container) / <alpha-value>)",
      secondary: "rgb(var(--color-secondary) / <alpha-value>)",
      onSecondary: "rgb(var(--color-on-secondary) / <alpha-value>)",
      secondaryContainer: "rgb(var(--color-secondary-container) / <alpha-value>)",
      onSecondaryContainer: "rgb(var(--color-on-secondary-container) / <alpha-value>)",
      tertiary: "rgb(var(--color-tertiary) / <alpha-value>)",
      onTertiary: "rgb(var(--color-on-tertiary) / <alpha-value>)",
      tertiaryContainer: "rgb(var(--color-tertiary-container) / <alpha-value>)",
      onTertiaryContainer: "rgb(var(--color-on-tertiary-container) / <alpha-value>)",
      error: "rgb(var(--color-error) / <alpha-value>)",
      onError: "rgb(var(--color-on-error) / <alpha-value>)",
      errorContainer: "rgb(var(--color-error-container) / <alpha-value>)",
      onErrorContainer: "rgb(var(--color-on-error-container) / <alpha-value>)",
      background: "rgb(var(--color-background) / <alpha-value>)",
      onBackground: "rgb(var(--color-on-background) / <alpha-value>)",
      surface: "rgb(var(--color-surface) / <alpha-value>)",
      onSurface: "rgb(var(--color-on-surface) / <alpha-value>)",
      surfaceVariant: "rgb(var(--color-surface-variant) / <alpha-value>)",
      onSurfaceVariant: "rgb(var(--color-on-surface-variant) / <alpha-value>)",
      outline: "rgb(var(--color-outline) / <alpha-value>)",
      outlineVariant: "rgb(var(--color-outline-variant) / <alpha-value>)",
      shadow: "rgb(var(--color-shadow) / <alpha-value>)",
      scrim: "rgb(var(--color-scrim) / <alpha-value>)",
      white: "rgb(var(--color-white) / <alpha-value>)",
      inverseSurface: "rgb(var(--color-inverse-surface) / <alpha-value>)",
      inverseOnSurface: "rgb(var(--color-inverse-on-surface) / <alpha-value>)",
      inversePrimary: "rgb(var(--color-inverse-primary) / <alpha-value>)",
      primaryFixed: "rgb(var(--color-primary-fixed) / <alpha-value>)",
      onPrimaryFixed: "rgb(var(--color-on-primary-fixed) / <alpha-value>)",
      primaryFixedDim: "rgb(var(--color-primary-fixed-dim) / <alpha-value>)",
      onPrimaryFixedVariant: "rgb(var(--color-on-primary-fixed-variant) / <alpha-value>)",
      secondaryFixed: "rgb(var(--color-secondary-fixed) / <alpha-value>)",
      onSecondaryFixed: "rgb(var(--color-on-secondary-fixed) / <alpha-value>)",
      secondaryFixedDim: "rgb(var(--color-secondary-fixed-dim) / <alpha-value>)",
      onSecondaryFixedVariant: "rgb(var(--color-on-secondary-fixed-variant) / <alpha-value>)",
      tertiaryFixed: "rgb(var(--color-tertiary-fixed) / <alpha-value>)",
      onTertiaryFixed: "rgb(var(--color-on-tertiary-fixed) / <alpha-value>)",
      tertiaryFixedDim: "rgb(var(--color-tertiary-fixed-dim) / <alpha-value>)",
      onTertiaryFixedVariant: "rgb(var(--color-on-tertiary-fixed-variant) / <alpha-value>)",
      surfaceDim: "rgb(var(--color-surface-dim) / <alpha-value>)",
      surfaceBright: "rgb(var(--color-surface-bright) / <alpha-value>)",
      surfaceContainerLowest: "rgb(var(--color-surface-container-lowest) / <alpha-value>)",
      surfaceContainerLow: "rgb(var(--color-surface-container-low) / <alpha-value>)",
      surfaceContainer: "rgb(var(--color-surface-container) / <alpha-value>)",
      surfaceContainerHigh: "rgb(var(--color-surface-container-high) / <alpha-value>)",
      surfaceContainerHighest: "rgb(var(--color-surface-container-highest) / <alpha-value>)",
      brand: "rgb(var(--color-brand) / <alpha-value>)",
      onBrand: "rgb(var(--color-on-brand) / <alpha-value>)",
      brandContainer: "rgb(var(--color-brand-container) / <alpha-value>)",
      onBrandContainer: "rgb(var(--color-on-brand-container) / <alpha-value>)",
    },
  },
  plugins: [],
}

