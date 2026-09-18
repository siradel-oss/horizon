import { cva, type VariantProps } from "class-variance-authority";

export const badgeVariants = cva(
    "inline-flex items-center rounded-md border px-2 py-0.5 text-xs font-medium transition-colors",
    {
        variants: {
            variant: {
                default: "border-transparent bg-primary text-primary-foreground",
                secondary: "border-transparent bg-secondary text-secondary-foreground",
                destructive: "border-transparent bg-destructive text-destructive-foreground",
                success: "border-transparent bg-success text-success-foreground",
                warning: "border-transparent bg-warning text-warning-foreground",
                outline: "text-foreground",
            },
        },
        defaultVariants: {
            variant: "default",
        },
    }
);

export type BadgeVariants = VariantProps<typeof badgeVariants>;
