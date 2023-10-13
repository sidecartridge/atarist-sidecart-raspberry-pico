# Changelog

## 0.0.10 (2023-10-13) - Feature aditions release, Update recommended
### Fixed
No fixes in this release

### Changed
- Atari ST firmware app now reports the meaning of the flashing LED.
- Atari ST firmware app now allows to toggle the Delay/Ripper mode from the menu.

### Feature aditions
- issue #7: Floppy drive emulation of .ST files now supports Read and Write in a PREVIEW mode to get feedback from the community. I have found some glitches and sometimes it fails. Please report any issues you find. Please note that this feature can make you loss the content of your content. Use it at your own risk.
- issue #25: New SAFE_CONFIG_REBOOT parameter. If true, pressing the SELECT button will not cause an immediate reboot. Instead, the RP2040 will change the board's status for the subsequent Atari ST power cycle, ensuring a smooth transition without disrupting ongoing processes. If false, the RP2040 will reboot immediately, as it did before.


## 0.0.9 (2023-09-29) - Bug fix release, Update recommended
### Fixed
- issue #9: Delete key now deletes in open fields.
- issue #8: Configuration mode now always looks for Wifi networks.

### Changed
- Atari ST firmware app now resets the computer when leaving the application.
- Atari ST firmware app now also uses shortcut keys for the menu, not only numerical.
- Release two versions of the firmware app, one with debug traces and other without.

### Feature aditions
- issue #6: 'Ultimate-Ripper' behaviour implement. New DELAY_ROM_EMULATION flag.
- issue #7: Read-only floppy drive emulation of .ST files is now supported as a PREVIEW feature to get feedback from the community. I have found some glitches and sometimes it fails. Please report any issues you find.

## 0.0.8 (2023-09-15) - Feature release
- First version
