## Agent skills

### Issue tracker

Issues and PRDs are tracked in GitHub Issues for `Driadix/ShuttleControllerV3`. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the canonical `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix` labels. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.

### Model routing

- Simple subagent tasks - explore, simple research, docs analysis, another point of view: Deepseek V4 Flash (default) or GPT 5.6 Luna xhigh (backoff).
- Main work - architectural decisions, code review, deep analysis, orchestration, vital decisions: GPT 5.6 Sol high.
