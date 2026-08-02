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

## UTF-8 safety on Windows

Issue titles, bodies, and comments contain Cyrillic text.
Windows PowerShell 5.1 may decode UTF-8 output from native commands with a legacy console code page.
Piping `gh` JSON into `ConvertFrom-Json` and writing the result back can therefore permanently store mojibake on GitHub even when the source text was valid.

The following rules are mandatory for GitHub writes containing non-ASCII text:

- Compose every multi-line body or comment in a UTF-8 Markdown file and pass it to `gh` with `--body-file`.
- Use the file directly as the write source; do not read a GitHub body through a native PowerShell pipeline, modify it in memory, and write it back.
- Prefer `gh --json ... --jq ...` when a query can be completed inside `gh` without a PowerShell JSON round trip.
- If PowerShell must parse `gh` JSON, set UTF-8 before the first native command as shown below.
- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Test-GitHubIssueText.ps1` after every mutation of an issue, comment, or map.
- Fetch the mutated issue again and verify its full body, not only the `gh` exit code or URL.

Safe write example:

```powershell
gh issue edit 1 --body-file "C:\Users\Driad\AppData\Local\Temp\opencode\issue-1.md"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Test-GitHubIssueText.ps1
gh issue view 1 --json body --jq .body
```

Required PowerShell 5.1 setup when a native JSON pipeline is unavoidable:

```powershell
$utf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8

$issue = gh issue view 1 --json body | ConvertFrom-Json
```

Never run the final line without the preceding encoding setup.
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
