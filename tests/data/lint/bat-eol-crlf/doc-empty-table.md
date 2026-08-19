# Fixture design document -- an allowlist that archives something unrelated

The table has to parse (an unparseable section 11.4 is exit 3, which is a
different verdict), but it must not archive the candidate under test.

### 11.4 门的定义、谓词、枚举表

> * **P2**: library functions known to reach application code -- `setGeometry` / `add<T>`.

| # | 位置 | 门原语 | 门后经 `this`/成员的读写 | 级 | 轮 |
|---|---|---|---|---|---|
| F1 | `Nothing.cpp`（`somethingElse`） | P1 | yes | S3 | W3 |

### 11.5 开销量化
