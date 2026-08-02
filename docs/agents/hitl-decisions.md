# HITL Decision Protocol

This protocol applies when an agent works through a ticket with a human decision-maker, especially a `wayfinder:grilling` ticket.

Its purpose is to make the owner an informed participant in the decision rather than a source of short confirmations.

## Research before asking

Before asking a question, investigate every relevant fact available from the repository, production evidence, domain glossary, ADRs, closed tickets, research assets, and primary external sources.

Do not ask the owner for a fact that the agent can obtain independently.

Do not present a decision until its dependency decisions and known constraints have been loaded.

## Decision briefing

Before every meaningful product, system, architecture, safety, protocol, verification, or lifecycle choice, explain:

- what is being decided and which later work depends on it;
- why the decision is needed now rather than safely deferrable;
- which facts, prior decisions, assumptions, and unknowns shape it;
- why the presented alternatives cover the relevant decision space;
- how each alternative affects safety, determinism, architecture, coupling, ownership, testability, maintainability, extensibility, scalability, resource overhead, implementation, diagnostics, operations, and reversibility where relevant;
- which alternative the agent recommends and why;
- the strongest argument against the recommendation;
- the conditions or new evidence under which the recommendation should change.

Use only the dimensions that materially distinguish the alternatives.
Do not add a ceremonial section for an irrelevant dimension.

## Evidence language

Separate the basis of the briefing explicitly:

- **Fact:** confirmed by code, a primary source, measurement, or an accepted decision.
- **Assumption:** currently treated as true but not yet confirmed.
- **Judgment:** the agent's engineering interpretation or recommendation.
- **Unknown:** missing information that could change the decision.

Do not describe an analytical estimate as a measurement or a conditional result as proven.
State confidence and validation obligations when evidence is incomplete.

## Alternatives and recommendation

Do not offer arbitrary options merely to create a multiple-choice question.
Explain how the alternatives were derived and why omitted possibilities are equivalent, dominated, premature, or out of scope.

Recommend a specific option whenever the available evidence supports one.
Do not hide behind a neutral list.

Counter anchoring bias by presenting the strongest case against the recommendation and the circumstances in which another option would be preferable.

A useful recommendation has this shape:

> I recommend A under constraints X and Y. Its main disadvantage is Z. If Q becomes the priority or assumption R is disproved, B becomes preferable.

## Asking the question

Ask one decision question at a time.
The question must be understandable from the briefing and must identify the exact choice being confirmed.

For a consequential or hard-to-reverse decision, use a full briefing.
For a terminology clarification, confirmation of an already established conclusion, or low-impact reversible choice, use a short rationale instead of repeating the full structure.

Never omit the rationale solely because the ticket question is already precise or terminology-heavy.

## Recording the resolution

The resolution comment must preserve enough context for a later session to understand the decision without replaying the conversation.

Record:

- the decision and its scope;
- the decisive facts and constraints;
- the criteria used;
- the selected alternative and rationale;
- materially different rejected alternatives and why they lost;
- assumptions, unknowns, confidence, and validation obligations;
- conditions that require reconsideration;
- links to supporting evidence and assets.

The issue holds the detailed rationale.
A wayfinder map contains only a one-line context pointer to that resolution.

## Recommended briefing shape

Use this as a guide, not as mandatory boilerplate:

```markdown
### Decision

What is being decided and why it matters now.

### Basis

Facts, accepted constraints, assumptions, unknowns, and confidence.

### Alternatives

Why these alternatives, with the material benefits, costs, and risks of each.

### Recommendation

The recommended option, strongest counterargument, trade-offs, and reconsideration conditions.

### Question

One exact choice for the owner.
```
