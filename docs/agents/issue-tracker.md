# Issue tracker: GitHub

Issues and PRDs for this repo live as GitHub issues. Use the `gh` CLI for all operations.

## Conventions

- **Create an issue**: write the body to a UTF-8 Markdown file, then run `gh issue create --title "..." --body-file <path>`.
- **Read an issue**: `gh issue view <number> --comments`, filtering comments by `jq` and also fetching labels.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Comment on an issue**: write the comment to a UTF-8 Markdown file, then run `gh issue comment <number> --body-file <path>`.
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: post any non-ASCII resolution through `gh issue comment <number> --body-file <path>`, verify it, then run `gh issue close <number>`.

Infer the repo from `git remote -v`; `gh` does this automatically inside the clone.

## UTF-8 safety

Issue titles, bodies, and comments contain Cyrillic text.
OpenCode's Bash tool runs Git Bash, which passes UTF-8 through natively, so `gh --json ... --jq ...` pipelines are safe.
The remaining rule for writes:

- Compose every multi-line body or comment in a UTF-8 Markdown file and pass it to `gh` with `--body-file`.
- Use the file directly as the write source; do not read a GitHub body through a shell pipeline, modify it in memory, and write it back.
- After a write, fetch the mutated issue again and verify its full body, not only the `gh` exit code or URL.

Safe write example:

```bash
gh issue edit 1 --body-file "/c/Users/Driad/AppData/Local/Temp/opencode/issue-1.md"
gh issue view 1 --json body --jq .body
```

If mojibake is ever suspected, run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Test-GitHubIssueText.ps1` as a diagnostic.
For a read-modify-write operation, prefer reconstructing the intended Markdown in a reviewed UTF-8 file instead of transforming fetched text.

## Pull requests as a triage surface

**PRs as a request surface: no.**

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> --comments` and `gh pr diff <number>`.
- **List external PRs for triage**: `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments`, keeping only `CONTRIBUTOR`, `FIRST_TIME_CONTRIBUTOR`, or `NONE`.
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`.

GitHub shares one number space across issues and PRs. Resolve an ambiguous `#42` with `gh pr view 42`, then fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments`.

## Wayfinding operations

Used by `/wayfinder`. The map is a single issue with child issues as tickets.

- **Map**: an issue labelled `wayfinder:map`, holding Notes, Decisions-so-far, and Fog. Create it with `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue. Where sub-issues are unavailable, add it to a task list in the map body and put `Part of #<map>` at the top of the child. Use `wayfinder:<type>` labels: `research`, `prototype`, `grilling`, or `task`.
- **Blocking**: use GitHub native issue dependencies. Where unavailable, put `Blocked by: #<n>, #<n>` at the top of the child body.
- **Frontier query**: choose the first open, unassigned map child without an open blocker.
- **Claim**: `gh issue edit <n> --add-assignee @me`.
- **Resolve**: comment with the answer, close the ticket, then append a context pointer to the map's Decisions-so-far.
