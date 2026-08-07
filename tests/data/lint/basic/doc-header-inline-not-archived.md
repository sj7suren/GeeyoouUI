# Fixture design document

Everything before section 11.4 must be ignored by the parser, including tables:

| # | 位置 | 门 | 门后 | 级 | 轮 |
|---|---|---|---|---|---|
| Z1 | `Thing.cpp`（`unguardedDoorThenMemberRead`） | P1 | yes | S1 | W1 |

That row is in a section the allowlist does not cover, so it must NOT archive
anything. A parser that scans the whole file would archive it and the lint would
go green for a reason nobody wrote down.

### 11.4 门的定义、谓词、枚举表

> * **P2**: library functions known to reach application code -- `setGeometry` / `add<T>`.

| # | 位置 | 门原语 | 门后经 `this`/成员的读写 | 级 | 轮 |
|---|---|---|---|---|---|
| F1 | `Thing.cpp`（`unguardedDoorThenMemberRead`） | P1 `onDecorated()` | `count_` | S2 | W2 |
| F2 | `Thing.cpp`（`overrideOnlyVirtualIsStillADoor`） | P1 `layoutRect()` | `count_` | S3 | W3 |
| F3 | `Thing.cpp`（`qualifiedP2CallIsStillADoor`） | P2 `setGeometry()` | `count_` | S3 | W3 |
| F5 | `Thing.hpp`（`adopt`） | P2 `add<T>()` | `count_` | S3 | W3 |

### 11.5 开销量化

Section 11.5 ends the allowlist. A row here must NOT archive anything either:

| # | 位置 | 门 | 门后 | 级 | 轮 |
|---|---|---|---|---|---|
| Z2 | `Thing.cpp`（`doorNameOnlyInAComment`） | P1 | yes | S1 | W1 |
