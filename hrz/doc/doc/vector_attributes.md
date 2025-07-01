---
Title: Attributes
Category: Vectors
---

## Types

Attribute values can be of any of these types:

* `null`: denotes a missing value.
* `boolean`: either true or false.
* `number`: an IEEE-754 64-bit floating point number. Same as JavaScript's Number type.
* `int64`: a 64-bit signed integer. Only required for very large numbers, whose absolute value is greater than 2^53-1.
* `uint64`: a 64-bit unsigned integer. Only required for very large numbers, like H3 grid IDs.
* `string`: an string.

Note that there is some overlap: [safe integers](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Number/MAX_SAFE_INTEGER) can be stored as both a `number` and a `(u)int64` (depending on its sign).

Color values can be stored as `0xaabbggrr` in any numeric type (as they are all safe integers), or as HTML color string or case-insensitive [names](https://developer.mozilla.org/en-US/docs/Web/CSS/named-color) (`#rgb`, `#rrggbbaa`, `Red`, `blue`, etc.) in strings when using the `TO_COLOR` [[AttributeTransform]] or the `to_color` styling script function. (See [transforms](#transforms).)

!!! note "Retrieving attribute values"
    Internally, the engine can decide to store values in whatever type can fit it without precision loss. This means that for example when the client sends a value of `int64 = 24`, the engine could later return it as `number = 24`.

    The TypeScript API [provides helpers](#helpers) to ease fetching such values.

## Missing values

When loading attributes, missing values in the source are given the null value.

## Transforms

Since the input attributes can be of heterogeneous type, it is often necessary to cast them to whatever type is necessary in styling scripts using functions such as `to_string`, `to_color`, etc. However, this incurs a runtime cost everytime the script is executed.

Instead, when the user knows in advance that all attributes will be casted to a given type, it is possible to specify the transformation to apply once, when the data is loaded, so that the styling scripts can assume the values are of homogeneous type. This is done using [[AttributeTransform]]. The transform can be given:

* In [[InMemoryAttribute]] for attributes originating from in-memory vector data layers.
* In [[ThreeDTileAttribute]] for attributes originating from 3D Tiles batch tables.
* In [[VectorAttribute]] for all other sources.

The available transforms are described on the [[AttributeTransform]] enumeration.

## Helpers

The `@siradel/horizon-protocol` TypeScript package provides utilities to work with [attribute values](HrzProtocol.AttributeValue.html):

!!! note ""
    [`HrzProtocol.IAttributeValue`](HrzProtocol.AttributeValue.html) and [`HrzProtocol.IInMemoryAttributeValue`](HrzProtocol.InMemoryAttributeValue.html) can be used interchangeably in the TypeScript API as they have the same interface.

### `attributeAsNumber`

```ts
attributeAsNumber(value: HrzProtocol.IAttributeValue) : number
```

Uses the first non-null field as number, parsing and casting as necessary.

### `attributeAsAny`

```ts
attributeAsAny(value: HrzProtocol.IAttributeValue) : number|Long|string|null
```

Returns the first non-null field value, or null if none is defined.
