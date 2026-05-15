## Summary
<!-- 1-3 bullets: what changes, why now. Link the relevant ADR/issue. -->

## Type
- [ ] feat
- [ ] fix
- [ ] refactor
- [ ] perf
- [ ] docs / chore / test / build / ci

## Surface area
<!-- Which modules are touched? engine / domain / plugin / platform / ui / app / tests -->

## Test plan
- [ ] Unit tests added/updated for changed code
- [ ] CI matrix green (Windows + Linux, Debug + Release)
- [ ] Manual smoke (if UI/runtime behavior changed)

## Risk
<!-- What could regress? Anything that needs a backout plan? -->

## Checklist
- [ ] No new warnings (`/W4`, `-Wall -Wextra -Wpedantic`)
- [ ] Public headers documented (intent, not what)
- [ ] Errors returned via `Result<T>`, not exceptions across module boundaries
- [ ] New tunables registered with `FeatureFlag` / `Config`
- [ ] ADR added if a cross-cutting decision changed
