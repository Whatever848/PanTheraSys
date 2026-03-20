# GitHub Bootstrap

Use the following sequence after authenticating to GitHub on a workstation that has `git` and optionally `gh`.

## Manual Private Repository Creation

1. Create a new private repository on GitHub, for example `PanTheraSys`.
2. Copy the remote URL.
3. From `D:\PanTheraSys`, run:

```powershell
git add .
git commit -m "chore: bootstrap PanTheraSys architecture skeleton"
git remote add origin <your-private-repo-url>
git push -u origin main
```

## Optional GitHub CLI Flow

```powershell
gh auth login
gh repo create PanTheraSys --private --source . --remote origin --push
```

## Recommended Branch Protection

- Protect `main`
- Require pull requests
- Require status checks: `windows-ci`, `codeql`
- Restrict force pushes
- Require review for `release/*` and `hotfix/*`
