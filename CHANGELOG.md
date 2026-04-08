# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

### Added

### Removed

### Fixed

## [n/a] 8 April 2026

### Changed
- Changed the order that .LYP was generated, now matching order in TOML

## [n/a] 7 April 2026

### Changed
- Major change to TOML file keywords for Z hight defnintion:
	- Added `align_bottom_to` and `alighn_top_to` keywords to assist with snapping
	- Allows arbirary placement of `z_start_nm = 0.0` in the stack, and accumulates both negative and postive high water marks
- Updated documentation and alignment with spec versions
- Updated code version (in CMake).

## [n/a] 6 April 2026

### Changed
- TOML file now accumulates Z dimention in reverse order
  - The last `[[layer]]` will be at the bottom of the z stack, and the first will be at the top

## [n/a] 27 March 2026

### Added
- Documentation
  - README, thickness/z_location "cheat sheet"

## [n/a] 26 March 2026

### Added
- Spec now v0_4_1
- Included CLAUDE.md
- Phase 0-2 complete
- Phase 3 complete
- Phase 4 complete
- Phase 5 complete
- Phase 6 complete
- Initial testing looks good
