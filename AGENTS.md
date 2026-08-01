## Agent skills

### Issue tracker

Issues and PRDs are tracked in GitHub Issues for `Driadix/ShuttleControllerV3`. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the canonical `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix` labels. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.

### Model routing

- Default model for all work: Deepseek V4 Flash Free Max (opencode).
- Backoff: GPT 5.6 Luna xhigh.
- Use the default (Deepseek) unless it is not working, or you judge the task would benefit from GPT Luna - then switch to GPT Luna.
