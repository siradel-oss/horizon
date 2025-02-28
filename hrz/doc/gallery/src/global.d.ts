export {};

declare module "vue" {
    interface ComponentCustomProperties {
        $filters: {
            formatNumber: (value: number, decimals?: number) => string;
        };
    }
}
