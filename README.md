# VIP system
My Discord server - https://discord.com/invite/g798xERK5Y

List of modules - [click](https://github.com/Pisex/cs2-vip-modules)
# TODO List
- [ ]  ...

## Require
- CS2 Server (Linux or Windows)
- sql_mm plugins by zer0k-z : https://github.com/zer0k-z/sql_mm (In the release archive)
- Specify database data in the config

Those who can, please put [Accelerator](https://github.com/komashchenko/AcceleratorLocal/releases/tag/v1.0.0) and if the server crashes, then send the crash file so I can understand what caused it

## Commands
Client command:
- `!vip` - plugin main menu

The plugin includes the following console commands:
- `vip_reload` - reload vip config with groups
- `vip_remove <userid|nickname|accountid>` - take away vip access from the player
- `vip_give <userid|nickname|accountid> <time_second> <group>` - give vip access to a player

## Configuration
- Databases file: `addons/configs/databases.cfg`
- Groups file: `addons/configs/vip/groups.ini`
- Translation file: `addons/translations/vip.phrases.txt`
