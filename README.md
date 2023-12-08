# VIP system
My Discord server - https://discord.com/invite/g798xERK5Y
# TODO List
- [ ]  ...

## Require
- CS2 Server (Linux or Window)
- Remove server hibernation `sv_hibernate_when_empty 0`
- mysql_mm plugins by Poggu : https://github.com/Poggicek/mysql_mm (In the release archive)
- Specify database data in the config

Those who can, please put [Accelerator](https://github.com/komashchenko/AcceleratorLocal/releases/tag/v1.0.0) and if the server crashes, then send the crash file so I can understand what caused it

## Commands
Client command:
- `!vip` - plugin main menu

The plugin includes the following console commands:
- `vip_reload` - reload vip config with groups
- `vip_remove <userid|nickname|accountountid>` - take away vip access from the player
- `vip_give <userid|nickname|accountid> <time_second> <group>` - give vip access to a player

## Configuration
- Databases file: `addons/configs/databases.cfg`
- Translation file: `addons/translations/vip.phrases.txt`
