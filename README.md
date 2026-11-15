# TA990SA DriverOS Modification

We sperate this instruction into several modules. A general module hierarchy is like:

```
$module/
  -> build/ <-- exported binary
  -> README.md <-- Instruction
```

## FileSystem Module (`fs`)

We use `mergerfs` to merge fs into one "path".