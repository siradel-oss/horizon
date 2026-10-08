Generated with:

```
pnpm dlx shadcn-vue@latest apply a2SNkvgm
pnpm dlx shadcn-vue@latest add badge button card checkbox dialog input progress select table tabs
```

`tailwind.css` is a copy of `shadcn-vue/dist/tailwind.css` (shadcn-vue 2.8.2), vendored so that `shadcn-vue` and its
dependency tree don't have to be installed. When adding components, check whether they rely on variants or utilities
missing from this copy, and re-sync it from the latest `shadcn-vue` package if so.
