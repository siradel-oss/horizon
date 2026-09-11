+++
title = "Placeholder symbol element"
+++

# Placeholder symbol element

The [PlaceholderSymbolElement]($proto) serves little purpose and is useful mostly as a temporary element. This element is used to render a rectangle of the given size and colour. It also has the particularity of being the default element when creating a symbol or any child element with an unspecified type. Ultimately, placeholders can be used to quickly prototype symbols.

> [!note]
> It isn't recommended to use a placeholder to visually render a rectangle (outside of prototyping). It is better to use the [DecoratedShapeSymbolElement]($proto) element because it has antialiased edges and more parameters to tweak the final visual.

