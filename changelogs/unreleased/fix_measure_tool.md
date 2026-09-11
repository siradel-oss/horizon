# Added

* `uint64Equals()` has been added to `HrzProtocolHelper` for comparing two `uint64` values.

# Integration notes

* Use `HrzProtocolHelper.uint64Equals()` to compare two `uint64` values from the protocol, such as layer handles. (`==` and `===` do not work.)
