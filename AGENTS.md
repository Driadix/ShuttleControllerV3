## Agent skills

### Issue tracker

Issues and PRDs are tracked in GitHub Issues for `Driadix/ShuttleControllerV3`. See `docs/agents/issue-tracker.md`.

GitHub titles, bodies, and comments contain Cyrillic text. Follow the mandatory UTF-8 workflow in `docs/agents/issue-tracker.md`; never round-trip `gh` output through an unconfigured Windows PowerShell 5.1 pipeline.

### HITL decisions

Before asking the owner to choose among meaningful product, system, or architecture alternatives, provide an evidence-based decision briefing and your recommendation. Follow `docs/agents/hitl-decisions.md`.

### Triage labels

Triage uses the canonical `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix` labels. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.

### Model routing

- Simple subagent tasks - explore, simple research, docs analysis, another point of view: Deepseek V4 Flash (default) or GPT 5.6 Luna xhigh (backoff).
- Main work - architectural decisions, code review, deep analysis, orchestration, vital decisions: GPT 5.6 Sol high.
