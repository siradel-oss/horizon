// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

export {};

declare module "vue" {
    interface ComponentCustomProperties {
        $filters: {
            formatNumber: (value: number, decimals?: number) => string;
        };
    }
}
