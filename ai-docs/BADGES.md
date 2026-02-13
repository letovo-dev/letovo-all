# Dynamic Badges Documentation

This document explains the dynamic status badges used throughout the project's documentation.

## Overview

The project uses dynamic badges that automatically update to show real-time status of:
- **Build status** (GitHub Actions)
- **Test coverage** (Codecov)
- **Test execution status** (GitHub Actions)
- **Last commit date** (GitHub)

## Badge Types

### 1. Build Status Badge

```markdown
[![Build Status](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml/badge.svg)](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml)
```

**Shows**: Whether the latest build passed or failed  
**Updates**: Automatically on every commit/push  
**Location**: Main README.md  
**Source**: GitHub Actions workflow `docker-image.yml`

### 2. Coverage Badge (Codecov)

```markdown
[![codecov](https://codecov.io/gh/letovo-dev/letovo-all/branch/main/graph/badge.svg)](https://codecov.io/gh/letovo-dev/letovo-all)
```

**Shows**: Current code coverage percentage  
**Updates**: After every test run that uploads coverage data  
**Location**: All README files  
**Source**: Codecov (receives data from GitHub Actions `test-coverage` job)

### 3. Coverage Badge (shields.io)

```markdown
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?label=coverage)
```

**Shows**: Coverage percentage with custom styling  
**Updates**: Pulls data from Codecov  
**Location**: Status tables in README files  
**Source**: shields.io + Codecov API

### 4. Test Execution Badge

```markdown
![Tests](https://img.shields.io/github/actions/workflow/status/letovo-dev/letovo-all/docker-image.yml?branch=main&label=tests)
```

**Shows**: Status of test execution (passing/failing)  
**Updates**: On every workflow run  
**Location**: README files  
**Source**: GitHub Actions workflow status

### 5. Last Commit Badge

```markdown
![Last Commit](https://img.shields.io/github/last-commit/letovo-dev/letovo-all)
```

**Shows**: Time since last commit  
**Updates**: On every commit  
**Location**: Main README.md status table  
**Source**: GitHub API

## Where Badges Are Used

### Main README.md

Located at: `/README.md`

**Top section** - Quick status overview:
- Build status (clickable → GitHub Actions)
- Coverage status (clickable → Codecov dashboard)
- Test status (clickable → GitHub Actions)

**Status table** - Detailed metrics:
- Coverage percentage
- Build status
- Test status
- Last commit date

### Test Suite README

Located at: `/test/README.md`

**Top section** - Test-specific badges:
- Test coverage with unittest flag
- Tests status from GitHub Actions
- Coverage with Codecov logo

**Current Status table**:
- Code coverage percentage
- Test execution status
- Target coverage (70%)

### Coverage Report

Located at: `/test/COVERAGE_REPORT.md`

**Top section**:
- Coverage status badge
- Coverage graph visualization (sunburst chart)
- Inline coverage percentage

## How Badges Work

### Data Flow

1. **Code is pushed** to GitHub
2. **GitHub Actions** triggers `docker-image.yml` workflow
3. **Test coverage job** runs:
   - Builds code with coverage instrumentation
   - Runs pytest test suite
   - Generates coverage data with lcov
4. **Coverage data** uploaded to Codecov
5. **Badges update** automatically:
   - GitHub Actions badges update immediately
   - Codecov badges update within ~1 minute

### Badge Services

#### GitHub Actions Badges
- **Provider**: GitHub
- **Update speed**: Instant (after workflow completes)
- **URL pattern**: `https://github.com/{org}/{repo}/actions/workflows/{workflow}.yml/badge.svg`

#### Codecov Badges
- **Provider**: Codecov
- **Update speed**: ~1 minute after upload
- **URL pattern**: `https://codecov.io/gh/{org}/{repo}/branch/{branch}/graph/badge.svg`
- **Requires**: CODECOV_TOKEN in GitHub secrets

#### shields.io Badges
- **Provider**: shields.io (badge-as-a-service)
- **Update speed**: Fetches data on request (cached ~5 minutes)
- **URL pattern**: `https://img.shields.io/{service}/{params}`
- **Customizable**: Colors, labels, logos, styles

## Badge Customization

### Changing Badge Style

shields.io badges support various styles:

```markdown
<!-- Flat (default) -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main)

<!-- Flat Square -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?style=flat-square)

<!-- Plastic -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?style=plastic)

<!-- For the Badge -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?style=for-the-badge)

<!-- Social -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?style=social)
```

### Adding Logos

```markdown
<!-- Add Codecov logo -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?logo=codecov)

<!-- Add custom color -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?color=brightgreen)
```

### Custom Labels

```markdown
<!-- Change label text -->
![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?label=test%20coverage)
```

## Troubleshooting

### Badge Shows "Unknown"

**Cause**: Codecov hasn't received coverage data yet  
**Solution**: 
1. Check GitHub Actions `test-coverage` job completed successfully
2. Verify CODECOV_TOKEN is set in repository secrets
3. Wait ~1 minute for Codecov to process data

### Badge Shows "Error" or "Invalid"

**Cause**: Repository or workflow name incorrect  
**Solution**: 
1. Verify repository URL: `letovo-dev/letovo-all`
2. Verify workflow file: `docker-image.yml`
3. Check branch name: `main`

### Badge Not Updating

**Cause**: Cache or network issue  
**Solution**:
1. Hard refresh browser (Ctrl+Shift+R)
2. Check badge URL directly
3. Verify workflow is actually running

### Codecov Badge Shows 0%

**Cause**: Coverage data not uploaded or processed  
**Solution**:
1. Check workflow logs for "Upload coverage to Codecov" step
2. Verify `coverage.info` file was generated
3. Check Codecov dashboard for errors

## Adding New Badges

### Step 1: Choose Badge Type

Visit [shields.io](https://shields.io) for available badge types:
- GitHub Actions status
- GitHub commits, issues, PRs
- Code coverage (Codecov, Coveralls)
- Package version (npm, PyPI, Maven)
- License, downloads, etc.

### Step 2: Generate Badge URL

Use shields.io URL builder or manual pattern:

```
https://img.shields.io/{service}/{user}/{repo}/{metric}?{params}
```

### Step 3: Add to Documentation

```markdown
[![Badge Label](badge-image-url)](click-through-url)
```

### Step 4: Test Badge

1. Commit and push changes
2. View README on GitHub
3. Verify badge displays correctly
4. Click badge to verify link works

## Best Practices

### ✅ DO:
- Place badges at top of README for visibility
- Use clickable badges (wrap in link)
- Group related badges together
- Keep badge URLs readable
- Document what each badge means

### ❌ DON'T:
- Overuse badges (max 5-7 per page)
- Use badges that rarely update
- Forget to test badges after adding
- Use outdated badge services
- Make badges the only documentation

## Maintenance

### Regular Checks

- **Weekly**: Verify all badges display correctly
- **After major changes**: Update badge URLs if needed
- **When workflows change**: Update GitHub Actions badges

### Updating Badge URLs

If repository is renamed or workflow changes:

1. Update all badge URLs in:
   - `README.md`
   - `test/README.md`
   - `test/COVERAGE_REPORT.md`

2. Test each badge:
   - Click to verify link works
   - Check image displays
   - Verify data is current

## Additional Resources

- **shields.io**: https://shields.io - Badge generator
- **Codecov Docs**: https://docs.codecov.com/docs/status-badges - Coverage badges
- **GitHub Actions**: https://docs.github.com/en/actions - Workflow badges
- **Markdown Guide**: https://www.markdownguide.org/basic-syntax/ - Badge syntax

---

**Last Updated**: 2026-02-13  
**Status**: ✅ All badges configured and working
