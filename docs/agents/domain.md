# Domain Docs

How engineering skills consume this repo's domain documentation.

## Before exploring, read these

- **`CONTEXT.md`** at the repo root.
- **`docs/adr/`**: read ADRs that touch the area about to be changed.

If these files do not exist, proceed silently. Do not suggest creating them upfront. Domain-modeling skills create them lazily when terminology or decisions are resolved.

## File structure

This is a single-context repository:

```text
/
|-- CONTEXT.md
|-- docs/
|   `-- adr/
`-- src/
```

## Use the glossary's vocabulary

When naming a domain concept in an issue, specification, design, test, or code, use the canonical term defined in `CONTEXT.md`. Do not drift to explicitly rejected synonyms.

If a required concept is absent, reconsider whether the term belongs to the project or record the gap for domain modeling.

## Flag ADR conflicts

If proposed work contradicts an existing ADR, surface the conflict explicitly instead of silently overriding the decision.
