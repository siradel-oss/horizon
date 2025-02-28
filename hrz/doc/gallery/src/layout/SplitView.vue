<template>
    <div class="flex flex-row items-stretch gap-4">
        <div class="w-1/4 min-w-[400px] flex-none relative h-full">
            <div class="absolute inset-0 rounded-2xl bg-surface p-4 overflow-auto">
                <slot name="left" />
            </div>
        </div>
        <div class="flex-1 relative h-full">
            <div class="absolute inset-0 rounded-2xl overflow-clip">
                <slot name="right" />
            </div>
        </div>
    </div>
</template>
