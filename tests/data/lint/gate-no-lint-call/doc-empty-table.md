# Fixture design document -- an allowlist that archives nothing under test

The table has to parse (an unparseable section 11.4 is exit 3, which is a
different verdict) and the P2 clause has to parse (so is that one), because this
fixture is about the GATE, not about the candidate set. The source tree here is
empty on purpose: the only thing that may redden this run is the missing call.

### 11.4 door predicate and register

> * **P2**: library functions known to reach application code -- `setGeometry` / `add<T>`.

| # | location | door | after the door | grade | round |
|---|---|---|---|---|---|
| F1 | `Nothing.cpp`(`somethingElse`) | P1 | yes | S3 | W3 |

### 11.5 cost
